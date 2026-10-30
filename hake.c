/* SPDX-License-Identifier: GPL-3.0-or-later — Copyright (C) 2026 Zachary Blauser */
/*
hake (hako-edit) — a minimal modal text editor
Copyright (C) 2026 Zachary Blauser

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
				
/*** includes ***/
#define HAKE_VERSION "0.2.0"

/* POSIX + BSD names (popen, strdup, getline, st_mtim ...) under a strict -std=c99 */
#if !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#define popen _popen
#define pclose _pclose
#define getcwd _getcwd
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define read(fd, buf, n) _read(fd, buf, n)
#define write(fd, buf, n) _write(fd, buf, n)
#define mkdir(p, m) _mkdir(p)
#define realpath(p, r) _fullpath((r), (p), PATH_MAX)
#ifdef _MSC_VER
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#define strcasecmp _stricmp
#endif
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <dirent.h>
#include <unistd.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#include <stdlib.h>
static long hk_getline(char **lineptr, size_t *n, FILE *stream) {
	if (!lineptr || !n || !stream) return -1;
	if (!*lineptr || *n == 0) { *n = 256; *lineptr = realloc(*lineptr, *n); if (!*lineptr) return -1; }
	size_t len = 0; int c;
	while ((c = fgetc(stream)) != EOF) {
		if (len + 1 >= *n) {
			size_t nn = *n * 2;
			char *t = realloc(*lineptr, nn);
			if (!t) return -1;
			*lineptr = t; *n = nn;
		}
		(*lineptr)[len++] = (char)c;
		if (c == '\n') break;
	}
	if (len == 0 && c == EOF) return -1;
	(*lineptr)[len] = '\0';
	return (long)len;
}
#define getline hk_getline
#endif
#else
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include <pthread.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0, 0}
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define TAB_STOP 8
#define MAX_PANES 32
#define AI_HISTORY_MAX 1000
#define PASTE_BUFFER_MAX 65536

const char *HAKE_HELP_TEXT =
	"hake v" HAKE_VERSION " - the hako editor (hako-edit)\n\n"
	"Usage: hake [options] [file]\n\n"
	"Options:\n"
	"  --vim, --emacs, --nano   key mode for this run (rc: edit.keys=vim|emacs|nano)\n"
	"  --hl-dump <file>         print each line and its highlight classes\n"
	"  -h, --help               this help\n"
	"  -v, --version            version\n\n"
	"Commands (every key mode; nano ^T, emacs M-x):\n"
	"  :w [file]  :q  :q!  :wq  :e <file|dir>  :<line>  :s/a/b/g  :%s/a/b/g\n"
	"  :sp :vs    split          :kami        the file explorer\n"
	"  :rei       the hako agent  :console     a shell pane (console_sandbox=1 boxes it)\n"
	"  :build :test :run [cmd]   the runner pane; ]e [e jump to errors\n"
	"  :!cmd      output in a popup\n"
	"  :theme [name]             pick or set a theme (saved to the rc)\n"
	"  :config [key=value]       open the rc, or set one key in it\n"
	"  :set key=value            for this session (:set nu, :set nowrap)\n"
	"  :voice [on|off]           read replies aloud (hako)\n"
	"  :undo :redo :reg :help\n\n"
	"Vim keys (the default):\n"
	"  i I a A o O        insert        v V           visual\n"
	"  h j k l w b 0 $    move          gg G :<n>     top, bottom, line\n"
	"  x dd D C dw cw cc  delete        yy p P        yank, put\n"
	"  u Ctrl-R .         undo, redo, repeat           / n N * #  search\n"
	"  di( ci\" ya{ ...    text objects  m` '         marks\n"
	"  Ctrl-F Ctrl-B      page          Ctrl-O Tab    jump list\n"
	"  Ctrl-W s v w c 1-9 r k          windows\n"
	"  Esc                normal mode\n";

static const char *HK_EMACS_HELP =
	"Emacs keys (edit.keys=emacs or --emacs)\n\n"
	"  C-f C-b C-n C-p    move          M-f M-b       word\n"
	"  C-a C-e            line          M-< M->       buffer\n"
	"  C-v M-v            page          M-g           go to line\n"
	"  C-SPC              mark          C-w M-w       kill, copy region\n"
	"  C-k                kill line     C-y           yank\n"
	"  C-s C-r            search        M-%           replace\n"
	"  C-/                undo          C-g           cancel\n"
	"  C-x C-s            save          C-x C-w       save as\n"
	"  C-x C-f            find file     C-x C-c       quit\n"
	"  C-x b              buffers       C-x k         close buffer\n"
	"  C-x 2 3 o 0 1      windows       M-x           any : command\n\n";

static const char *HK_NANO_HELP =
	"Nano keys (edit.keys=nano or --nano)\n\n"
	"  ^O  write out       ^X  exit (asks to save)\n"
	"  ^W  where is        M-W find next\n"
	"  ^\\  replace         ^_  go to line\n"
	"  ^K  cut line        ^U  paste\n"
	"  ^^  mark (M-A)      M-6 copy\n"
	"  ^C  position        ^G  this help\n"
	"  ^R  insert a file   ^S  save\n"
	"  ^A ^E  line start, end        ^Y ^V  page\n"
	"  M-U M-E  undo, redo           M-< M->  other pane\n"
	"  ^T  a : command (:rei, :console, :theme ...)\n\n";

/*** enums ***/
enum editorMode {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL,
	MODE_VISUAL_LINE
};

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	MOUSE_WHEEL_UP,
	MOUSE_WHEEL_DOWN,
	MOUSE_CLICK,
	MOUSE_MOTION,
	CTRL_SPACE_KEY
};

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH,
	HL_PREPROCESSOR,
	HL_FUNCTION,
	HL_TYPE,
	HL_OPERATOR,
	HL_BRACKET,
	HL_CONSTANT,
	HL_BUILTIN,
	HL_ATTRIBUTE,
	HL_CHAR,
	HL_ESCAPE,
	HL_LABEL,
	HL_HEADING,
	HL_STRONG,
	HL_EMPH,
	HL_STRONGEMPH,
	HL_STRIKE,
	HL_CODE,
	HL_LINK,
	HL_URL,
	HL_MARKER,
	HL_QUOTE,
	HL_DONE,
	HL_DELIM,
	HL_DIFF_ADD,
	HL_DIFF_DEL,
	HL_VARIABLE,
	HL_TAG,
	HL_ATTRNAME,
	HL_KEY
};

enum paneType {
	PANE_EDITOR,
	PANE_EXPLORER,
	PANE_AI,
	PANE_RUNNER
};

enum terminalType {
	TERM_UNKNOWN,
	TERM_XTERM_256,
	TERM_TRUECOLOR,
	TERM_BASIC
};

enum keyMode {
	HK_KEYS_VIM,
	HK_KEYS_EMACS,
	HK_KEYS_NANO
};

enum splitDirection {
	SPLIT_NONE,
	SPLIT_HORIZONTAL,
	SPLIT_VERTICAL
};

/*** struct declarations ***/
typedef struct erow {
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
	int indent;
} erow;

struct abuf {
	char *b;
	int len;
	int cap;
};

/* r >= 0: true color. r == HK_COL_DEFAULT: the terminal's own fg/bg (nothing
   painted). r == HK_COL_ANSI: palette entry g (0-15), the profile's colors.
   a: HK_A_* attributes that travel with the color into the cell. */
typedef struct {
	int r, g, b;
	int a;
} Color;
#define HK_COL_DEFAULT (-2)
#define HK_COL_ANSI    (-3)
#define HK_A_BOLD   1
#define HK_A_ITALIC 2
#define HK_A_UNDER  4
#define HK_A_REV    8
#define HK_A_DIM    16
#define HK_A_STRIKE 32

typedef struct {
	char ch[5];
	Color fg;
	Color bg;
	signed char width;
} Cell;

typedef struct undoState {
	erow *rows;
	int numrows;
	int cx, cy;
	char *filename;
	char *yank_buffer;
	int yank_buffer_len;
	struct undoState *next;
	time_t timestamp;
} undoState;

typedef struct explorerData {
	char *current_dir;
	char **entries;
	int num_entries;
	int selected;
	int scroll_offset;
} explorerData;

typedef struct runnerData {
	char **lines;
	int count;
	int cap;
	int selected;
	int scroll_offset;
	int running;
	int exit_code;
	pid_t pid;
	FILE *fp;
	pthread_t thread;
	int thread_valid;
	int truncated;
	pthread_mutex_t lock;
	char cmd[512];
	int console;               /* :console: a prompt line under the output */
	char input[512];
	int input_len, input_cur;
	char **hist;
	int hist_n, hist_pos;
	char cwd[PATH_MAX];
} runnerData;

#define HK_ROLE_SYSTEM 0
#define HK_ROLE_USER   1
#define HK_ROLE_AI     2
#define HK_ROLE_TOOL   3      /* a tool call, hung off the rail */
#define HK_ROLE_TOOLOUT 4     /* what it gave back */
#define HK_ROLE_CONT   0x80   /* a wrapped or later line of the same message */

typedef struct aiData {
	char **history;
	unsigned char *history_role;
	int history_count;
	int scroll_offset;
	int spinner_frame;
	int cursor_y;
	enum editorMode mode;
	int visual_mode;
	int visual_start;
	int visual_end;
	char *prompt_buffer;
	int prompt_len;
	int prompt_cursor;
	int prompt_capacity;
	char *current_prompt;
	int streaming;
	pthread_mutex_t lock;
	int perm_pending;
	int perm_id;
	char perm_tool[64];
	char perm_kind[16];
	char perm_subject[1024];
	int cmd_sel;               /* the command menu over the input */
	int cmd_hide;              /* esc closed it for this line */
} aiData;

/* hako's command table, from its init (or a later state) message */
typedef struct { char n[32]; char p[2]; char c[16]; char a[32]; char d[96]; } hkCmd;
static hkCmd *hk_cmds;
static int hk_cmd_n;

typedef struct editorPane {
	enum paneType type;
	int id;
	
	int x, y;
	int width, height;
	
	int cx, cy;
	int rx;
	int rowoff, coloff;
	
	int numrows;
	erow *row;
	char *filename;
	int dirty;
	unsigned long long file_mtime; /* on-disk signature when loaded/saved; 0 = unknown */
	int disk_warned;              /* "changed on disk" said once for this buffer */
	struct editorSyntax *syntax;
	
	int visual_anchor_x, visual_anchor_y;
	
	undoState *undo_stack;
	undoState *redo_stack;
	int undo_stack_size;
	int redo_stack_size;
	
	explorerData *explorer;
	aiData *ai;
	runnerData *runner;
	
	char *pane_yank_buffer;
	int pane_yank_buffer_len;
	
	struct editorPane *parent;
	struct editorPane *child1;
	struct editorPane *child2;
	enum splitDirection split_dir;
	float split_ratio;
	
	int is_focused;
	int wrap_lines;
} editorPane;

typedef struct {
	Color bg;
	Color fg;
	Color comment;
	Color keyword1;
	Color keyword2;
	Color string;
	Color line_number;
	Color status_bg;
	Color status_fg;
	Color border;
	Color visual_bg;
	Color visual_fg;
	Color number;      /* not in the preset table: derived (the string color) */
	Color err;         /* derived: a red for the background */
} Theme;

struct editorSyntax {
	const char *filetype;
	const char *const *filematch;    /* ".ext", or a file name ("Makefile", ".bashrc") */
	const char *const *keywords;     /* "word", "word|" a type, "word!" a constant */
	const char *slc;                 /* line comment */
	const char *mcs, *mce;           /* block comment */
	int flags;                       /* HLF_* */
	int mode;                        /* HLM_*: a highlighter of its own */
	const char *quotes;              /* string delimiters; NULL is "\"'" */
	const char *slc2;                /* a second line comment */
};

typedef struct {
	char *query;
	int wrap_search;
} SearchState;

typedef struct pasteBuffer {
	char *data;
	int len;
	int is_line_mode;
} pasteBuffer;

struct editorConfig {
	int screenrows, screencols;
	enum editorMode mode;
#ifdef _WIN32
	int orig_in_mode;
	int orig_out_mode;
#else
	struct termios orig_termios;
#endif
	
	int tab_stop;
	int use_tabs;
	int word_wrap;
	int show_line_numbers;
	int pane_titles;              /* a title bar per editor pane (default on) */
	int line_number_width;
	int max_undo_levels;
	
	char statusmsg[256];
	time_t statusmsg_time;
	
	pasteBuffer paste;
	pasteBuffer system_paste;
	
	editorPane *root_pane;
	editorPane *active_pane;
	editorPane *left_panel;
	editorPane *right_panel;
	editorPane *bottom_panel;
	int num_panes;
	int left_panel_width;
	int right_panel_width;
	
	int in_window_command;
	int force_window_command;
	int intro_active;             /* the start screen with no file: gone on the first key */
	int full_redraw_pending;

	int keymode;                  /* HK_KEYS_VIM (default), HK_KEYS_EMACS, HK_KEYS_NANO */
	int keymode_forced;           /* --vim / --emacs / --nano: the rc does not change it */
	int status_line;              /* 0 never, 1 with more than one pane (default), 2 always */
	int status_shown;             /* drawn this frame */
	int console_sandbox;          /* the console runs in sandbox-exec / bwrap, or not at all */
	char *project_rc;             /* ./.hakorc when it was read */
	char *project_dir;            /* where hake started: the console's sandbox root */
	char rc_notice[160];          /* a project rc key that was refused */

	int popup_active;
	char **popup_lines;
	int popup_count;
	int popup_scroll;
	int popup_sel;
	int popup_selectable;
	int popup_preview;
	void (*popup_apply)(int sel);  /* a picker of our own: called with the pick on enter */
	long popup_choice_id;      /* >0: a Rei choice_request awaiting an answer */
	char popup_title[48];
	char popup_saved_theme[32];
	char theme_name[32];

	Cell *grid_front;
	Cell *grid_back;
	int grid_w, grid_h;

	char *explorer_name;
	char *ai_name;

	int explorer_width;
	int explorer_show_hidden;

	SearchState search;

	enum terminalType term_type;
	Theme theme;

	int is_pasting;

	int mouse_enabled;
	int mouse_x, mouse_y;

	char *hako_session_id;
	int hako_session_turns;
	int hako_session_resumed;
	int hako_protocol;
	char hako_session_name[64];
	int hako_remote;

	int auto_indent;
	int smart_indent;
	int scroll_speed;

	char *config_path;
	char *build_cmd;
	char *test_cmd;
	char *run_cmd;

	struct {
		int pending_count;
		char pending_reg;
		int waiting_reg;

		pasteBuffer registers[32];

		int marks_x[26], marks_y[26];
		int marks_set;

		struct { int x, y; char *filename; } jumps[100];
		int jump_count;
		int jump_pos;

		char last_op;
		int last_count;
		char *last_insert;
		int last_insert_len;
		int recording_insert;
	} hk;
};

struct editorConfig E;

/*** function prototypes ***/
void disableRawMode(void);
void editorCleanup(void);
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
void editorUpdateWindowSize(void);
int editorReadKey(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorSave(int force);
void editorOpen(char *filename);
void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
void editorUpdateRow(erow *row);
void editorFreeRow(erow *row);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorInsertChar(int c);
void editorDelChar(void);
void editorInsertNewLine(void);
void editorFind(void);
void editorFindNext(void);
void editorFindPrev(void);
void editorSaveState(void);
void editorUndo(void);
void editorRedo(void);
void editorYankVisualSelection(void);
void editorDeleteVisualSelection(void);
void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y);
void editorHandlePaste(const char *text, int len);
void editorScroll(void);
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorDrawPopup(void);
void editorPopupOpen(const char *title, const char *const *items, int count,
                     int selectable, int preview);
void editorPopupOpenHelp(void);
void editorPopupOpenThemes(void);
void editorPopupClose(int apply);
void editorPopupHandleKey(int c);
void editorProcessKeyPress(void);
int editorInputPending(void);
void editorLoadConfig(void);
void editorUpdateSyntax(erow *row);
void editorSelectSyntaxHighlight(void);
char *editorRowsToString(int *buflen);
int editorGetIndent(erow *row);
void editorSetMode(enum editorMode mode);
void editorColonCommand(void);
static void editorRunColon(char *cmd);
static void hkOpenPath(const char *path);
static int explorerOpenInSplit(const char *full_path);
static int hkFileExists(const char *p);
static int hkRcSplit(char *line, char **key, char **val);
static int hkApplyKey(const char *key, const char *val);
static int hkRcSet(const char *key, const char *val);
static void hkSaveTheme(const char *name);
static void hkConfigSet(const char *kv, int persist);
static void hkAgentSlash(const char *cmd);
static void hkConsoleToggle(void);
struct aiData;
static int hkReiMatches(struct aiData *data, int *out, int max);
static int hkReiMenuRows(struct editorPane *pane, int n);
static char *hkExtractJsonString(const char *src, const char *key);
static char *hkJsonUnescapeDup(const char *s);
int editorIsInVisualSelection(int row, int col);
void editorProcessMouse(int x, int y);
void editorHandleScroll(int direction);
void editorCopyToSystemClipboard(void);
void editorPasteFromSystemClipboard(void);
void editorFreePasteBuffer(void);
void editorSetPasteBuffer(const char *data, int len, int is_line_mode);
void hkSetRegister(char name, const char *data, int len, int is_line, int is_yank);
pasteBuffer *hkGetRegister(char name);
void hkFreeRegisters(void);
void hkPushJump(int x, int y);
void hkClampCursor(editorPane *pane);
static char *hakoFindBinary(void);
static int hakoLaunch(aiData *data);
static void hakoShutdown(void);
#define HAKE_TOOL_RESULT_LINES 10   /* tool output rows shown in the Rei pane */
static void hakoSendLine(const char *json);
static void hakoInstallOrUpdate(aiData *data, int update);
static void hakoHandleEvent(aiData *data, const char *line);
static void *hakoReaderThread(void *arg);
void aiWorkerSend(aiData *data);
int hkHandleSlash(aiData *data, const char *prompt);
static void runnerStart(const char *cmd);
static void hkCloseSidePanel(editorPane **panel, editorPane *fallback);
static void runnerCleanup(editorPane *pane);
static void runnerErrorStep(int dir);
static void hkShellPopup(const char *cmdline);
void runnerRender(editorPane *pane, struct abuf *ab);
void runnerHandleKey(editorPane *pane, int key);

editorPane *editorCreatePane(enum paneType type, int x, int y, int width, int height);
void editorFreePane(editorPane *pane);
void editorSplitPane(int vertical);
void editorClosePane(void);
void editorNextPane(void);
void editorResizePanes(void);
void editorUpdatePaneBounds(editorPane *pane);
void editorCollectLeafPanes(editorPane *root, editorPane ***panes, int *count);
editorPane *editorFindPaneById(editorPane *root, int id);
void editorDrawPane(editorPane *pane, struct abuf *ab);

undoState *editorCreateUndoState(void);
void editorFreeUndoState(undoState *state);
void editorRestoreState(undoState *state);

void explorerInit(editorPane *pane);
void explorerRefresh(editorPane *pane);
void explorerHandleKey(editorPane *pane, int key);
void explorerCleanup(editorPane *pane);
void explorerRender(editorPane *pane, struct abuf *ab);
void editorToggleExplorer(void);

void editorInitAI(void);
void editorCleanupAI(void);
void editorToggleAI(void);
void aiRender(editorPane *pane, struct abuf *ab);
void aiInit(editorPane *pane);
void aiHandleKey(editorPane *pane, int key);
void editorGenerateConfig(void);
void aiAddHistory(aiData *data, const char *text);
void aiAddHistoryRole(aiData *data, const char *text, unsigned char role);
int explorerCompare(const void *a, const void *b);

void initTheme(void);
int editorApplyThemeByName(const char *name);
void initEditor(void);
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void gridResize(int w, int h);
void gridFree(void);
void gridClear(Color bg);
void gridInvalidateFront(void);
int gridPutStr(int x, int y, const char *s, int max_cols, Color fg, Color bg);
int gridPutCh(int x, int y, char c, Color fg, Color bg);
void gridFlush(struct abuf *out);
int is_separator(int c);
void setThemeColor(struct abuf *ab, Color color);
void setThemeBgColor(struct abuf *ab, Color color);
Color editorSyntaxToRGB(int hl);
void detectTerminalType(void);
void enableRawMode(void);
int getWindowSize(int *rows, int *cols);
void die(const char *s);
int getDisplayWidth(const char *str, int len, int col);
int getCharWidth(char c, int col);
int utf8_byte_length(unsigned char c);
int utf8_char_width(const char *s, int pos);
int utf8_next_char(const char *s, int pos, int size);
int utf8_prev_char(const char *s, int pos);

#undef exit
#define exit(code) do { \
	editorCleanup(); \
	disableRawMode(); \
	fflush(stdout); \
	_exit(code); \
} while(0)

/*** syntax highlighting ***/
#define HLF_NUMBERS   (1 << 0)
#define HLF_STRINGS   (1 << 1)
#define HLF_PREPROC   (1 << 2)    /* # directives at the start of a line (C) */
#define HLF_NOCASE    (1 << 3)    /* keywords in any case (SQL, Dockerfile) */
#define HLF_TRIPLE    (1 << 4)    /* """ and ''' strings, over lines */
#define HLF_MLSTR     (1 << 5)    /* a quoted string may run onto the next line */
#define HLF_SQCHAR    (1 << 6)    /* ' is a char literal, else a lifetime or a prime */
#define HLF_RAWSQ     (1 << 7)    /* no escapes inside '...' */
#define HLF_DOLLAR    (1 << 8)    /* $var ${var} $(var) */
#define HLF_DECOR     (1 << 9)    /* @name: decorators, annotations, ivars */
#define HLF_NEST      (1 << 10)   /* block comments nest */
#define HLF_HASHWORD  (1 << 11)   /* the line comment only at the start of a word */
#define HLF_BACKTICK  (1 << 12)   /* `...` over lines (JS templates, Go raw) */
#define HLF_RAWBT     (1 << 13)   /* no escapes inside `...` */
#define HLF_BANG      (1 << 14)   /* name! is a macro (Rust) */
#define HLF_LUALONG   (1 << 15)   /* [[ ]] and --[[ ]] */
#define HLF_DASHID    (1 << 16)   /* - is part of a name (CSS, Lisp) */
#define HLF_SLC_BOL   (1 << 17)   /* line comments only as the first thing on a line */
#define HLF_JSONKEY   (1 << 18)   /* a string before ':' is a key */
#define HLF_MCS_BOL   (1 << 19)   /* block comments open at column 0 (Ruby =begin) */

enum { HLM_CODE, HLM_MARKDOWN, HLM_MARKUP, HLM_DIFF, HLM_CSS, HLM_MAKE, HLM_YAML, HLM_INI };

/* state carried from one line to the next: kind, nesting level, extra */
enum { ST_NONE, ST_COMMENT, ST_TDQ, ST_TSQ, ST_BT, ST_DQ, ST_SQ, ST_LUASTR, ST_LUACOM,
       ST_TAG, ST_TAGDQ, ST_TAGSQ };
#define ST_KIND(st) ((st) & 15)
#define ST_LVL(st)  (((st) >> 4) & 15)
#define ST_HI(st)   (((st) >> 8) & 255)
#define ST_MAKE(k, l, h) ((k) | (((l) & 15) << 4) | (((h) & 255) << 8))

static const char *const C_HL_extensions[] = {".c", ".h", NULL};
static const char *const C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"goto", "do", "case", "default", "static", "extern", "register", "auto",
	"inline", "restrict", "struct", "union", "typedef", "enum", "sizeof",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", "short|", "size_t|", "ssize_t|", "uint8_t|", "uint16_t|",
	"uint32_t|", "uint64_t|", "int8_t|", "int16_t|", "int32_t|", "int64_t|",
	"bool|", "_Bool|", "const|", "volatile|", "FILE|", "off_t|", "pid_t|",
	"NULL!", "EOF!", "true!", "false!", NULL
};

static const char *const CPP_HL_extensions[] = {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".C", ".h++", NULL};
static const char *const CPP_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"goto", "do", "case", "default", "static", "extern", "inline", "struct",
	"union", "typedef", "enum", "sizeof", "class", "public", "private", "protected",
	"virtual", "override", "final", "namespace", "using", "template", "typename",
	"try", "catch", "throw", "new", "delete", "this", "friend", "operator",
	"explicit", "export", "mutable", "noexcept", "constexpr", "decltype",
	"static_cast", "dynamic_cast", "reinterpret_cast", "const_cast", "co_await", "co_return",
	"int|", "long|", "double|", "float|", "char|", "bool|", "void|", "unsigned|",
	"signed|", "short|", "auto|", "const|", "size_t|", "std|", "string|", "vector|",
	"map|", "set|", "unique_ptr|", "shared_ptr|",
	"true!", "false!", "nullptr!", "NULL!", NULL
};

static const char *const PY_HL_extensions[] = {".py", ".pyw", ".pyi", NULL};
static const char *const PY_HL_keywords[] = {
	"def", "class", "if", "elif", "else", "while", "for", "in", "try",
	"except", "finally", "with", "as", "pass", "break", "continue",
	"return", "yield", "raise", "assert", "import", "from", "lambda",
	"async", "await", "and", "or", "not", "is", "del", "global", "nonlocal",
	"match", "case",
	"int|", "float|", "str|", "bool|", "list|", "dict|", "set|", "tuple|",
	"bytes|", "object|", "self|", "cls|",
	"None!", "True!", "False!", NULL
};

static const char *const JS_HL_extensions[] = {".js", ".jsx", ".mjs", ".cjs", NULL};
static const char *const JS_HL_keywords[] = {
	"function", "if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"var", "let", "const", "class", "extends", "new", "this", "super",
	"async", "await", "yield", "typeof", "instanceof", "in", "of", "delete",
	"import", "export", "from", "static", "get", "set", "void",
	"true!", "false!", "null!", "undefined!", "NaN!", "Infinity!", NULL
};

static const char *const TS_HL_extensions[] = {".ts", ".tsx", ".mts", ".cts", NULL};
static const char *const TS_HL_keywords[] = {
	"function", "if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"var", "let", "const", "class", "extends", "new", "this", "super",
	"async", "await", "yield", "typeof", "instanceof", "in", "of", "delete",
	"import", "export", "from", "as", "satisfies", "keyof",
	"interface", "type", "enum", "namespace", "module", "declare",
	"abstract", "implements", "private", "protected", "public", "readonly", "static",
	"string|", "number|", "boolean|", "any|", "void|", "never|", "unknown|", "object|",
	"true!", "false!", "null!", "undefined!", NULL
};

static const char *const JAVA_HL_extensions[] = {".java", NULL};
static const char *const JAVA_HL_keywords[] = {
	"class", "interface", "enum", "record", "extends", "implements", "package",
	"import", "public", "private", "protected", "static", "final",
	"abstract", "synchronized", "volatile", "transient", "var",
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "try", "catch", "finally", "throw", "throws",
	"new", "this", "super", "instanceof",
	"void|", "boolean|", "byte|", "char|", "short|", "int|", "long|",
	"float|", "double|", "String|", "true!", "false!", "null!", NULL
};

static const char *const GO_HL_extensions[] = {".go", NULL};
static const char *const GO_HL_keywords[] = {
	"func", "package", "import", "var", "const", "type", "struct",
	"interface", "map", "chan", "if", "else", "for", "range", "switch",
	"case", "default", "break", "continue", "goto", "return", "defer",
	"go", "select", "fallthrough",
	"int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|",
	"uint16|", "uint32|", "uint64|", "uintptr|", "float32|", "float64|", "bool|",
	"string|", "byte|", "rune|", "error|", "any|", "true!", "false!", "nil!", "iota!", NULL
};

static const char *const RUST_HL_extensions[] = {".rs", NULL};
static const char *const RUST_HL_keywords[] = {
	"fn", "let", "mut", "const", "static", "struct", "enum", "trait",
	"impl", "type", "pub", "use", "mod", "crate", "self", "super", "dyn",
	"as", "async", "await", "move", "ref", "unsafe", "where", "extern", "in",
	"if", "else", "match", "for", "while", "loop", "break", "continue", "return",
	"i8|", "i16|", "i32|", "i64|", "i128|", "isize|", "u8|", "u16|", "u32|", "u64|",
	"u128|", "usize|", "f32|", "f64|", "bool|", "char|", "str|", "String|", "Self|",
	"Vec|", "Option|", "Result|", "Box|",
	"true!", "false!", "Some!", "None!", "Ok!", "Err!", NULL
};

static const char *const RUBY_HL_extensions[] = {".rb", ".rake", ".gemspec", "Rakefile", "Gemfile", NULL};
static const char *const RUBY_HL_keywords[] = {
	"def", "class", "module", "if", "elsif", "else", "unless", "case",
	"when", "while", "until", "for", "in", "do", "begin", "rescue",
	"ensure", "end", "return", "yield", "break", "next", "redo", "retry",
	"require", "require_relative", "include", "extend", "attr_reader", "attr_writer",
	"attr_accessor", "alias", "super", "self", "then", "lambda", "proc",
	"and", "or", "not", "nil!", "true!", "false!", "__FILE__!", "__LINE__!", NULL
};

static const char *const SWIFT_HL_extensions[] = {".swift", NULL};
static const char *const SWIFT_HL_keywords[] = {
	"class", "struct", "enum", "protocol", "extension", "func", "var", "let",
	"if", "else", "switch", "case", "default", "for", "in", "while", "repeat",
	"break", "continue", "return", "guard", "defer", "do", "try", "catch",
	"throw", "throws", "import", "public", "private", "internal", "static", "final",
	"lazy", "weak", "unowned", "override", "init", "deinit", "subscript",
	"typealias", "associatedtype", "where", "some", "any", "async", "await",
	"Int|", "Double|", "Float|", "Bool|", "String|", "Character|", "Optional|",
	"Array|", "Dictionary|", "Set|", "Result|", "Self|",
	"true!", "false!", "nil!", "self", "super", NULL
};

static const char *const PHP_HL_extensions[] = {".php", ".phtml", NULL};
static const char *const PHP_HL_keywords[] = {
	"class", "function", "fn", "public", "private", "protected", "static",
	"if", "elseif", "else", "while", "for", "foreach", "switch", "case", "match",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"new", "extends", "implements", "interface", "abstract", "trait",
	"namespace", "use", "as", "const", "var", "global", "echo", "print", "require",
	"require_once", "include", "include_once",
	"int|", "float|", "string|", "bool|", "array|", "object|", "mixed|", "void|",
	"true!", "false!", "null!", "TRUE!", "FALSE!", "NULL!", NULL
};

static const char *const KOTLIN_HL_extensions[] = {".kt", ".kts", NULL};
static const char *const KOTLIN_HL_keywords[] = {
	"class", "interface", "fun", "object", "val", "var", "package", "import",
	"if", "else", "when", "for", "while", "do", "break", "continue", "return",
	"try", "catch", "finally", "throw", "public", "private", "protected",
	"internal", "open", "final", "abstract", "data", "sealed", "companion", "in", "is", "as",
	"Int|", "Long|", "Double|", "Float|", "Boolean|", "String|", "Char|",
	"Byte|", "Short|", "Any|", "Unit|", "Nothing|", "true!", "false!", "null!", NULL
};

static const char *const CS_HL_extensions[] = {".cs", NULL};
static const char *const CS_HL_keywords[] = {
	"class", "struct", "interface", "enum", "record", "namespace", "using", "public",
	"private", "protected", "internal", "static", "readonly", "const", "if", "else",
	"switch", "case", "default", "for", "foreach", "in", "while", "do", "break",
	"continue", "return", "try", "catch", "finally", "throw", "new", "this",
	"base", "virtual", "override", "abstract", "sealed", "async", "await", "var",
	"void|", "bool|", "byte|", "char|", "decimal|", "double|", "float|", "int|",
	"long|", "object|", "sbyte|", "short|", "string|", "uint|", "ulong|",
	"ushort|", "true!", "false!", "null!", NULL
};

static const char *const SCALA_HL_extensions[] = {".scala", ".sc", NULL};
static const char *const SCALA_HL_keywords[] = {
	"class", "object", "trait", "extends", "with", "def", "val", "var", "given", "using",
	"if", "else", "then", "match", "case", "for", "while", "do", "try", "catch",
	"finally", "throw", "return", "import", "package", "private", "protected",
	"public", "abstract", "final", "sealed", "implicit", "lazy", "override", "new", "yield",
	"Int|", "Long|", "Double|", "Float|", "Boolean|", "String|", "Char|",
	"Byte|", "Short|", "Any|", "Unit|", "Nothing|", "true!", "false!", "null!", NULL
};

static const char *const LUA_HL_extensions[] = {".lua", NULL};
static const char *const LUA_HL_keywords[] = {
	"and", "break", "do", "else", "elseif", "end", "for",
	"function", "if", "in", "local", "not", "or", "repeat",
	"return", "then", "until", "while", "goto",
	"self|", "_G|", "_ENV|", "true!", "false!", "nil!", NULL
};

static const char *const SHELL_HL_extensions[] = {".sh", ".bash", ".zsh", ".ksh", ".fish",
	".bashrc", ".zshrc", ".profile", ".bash_profile", ".zprofile", NULL};
static const char *const SHELL_HL_keywords[] = {
	"if", "then", "else", "elif", "fi", "for", "in", "while", "until", "do", "done",
	"case", "esac", "function", "select",
	"return|", "break|", "continue|", "exit|", "export|", "source|", "alias|", "unalias|",
	"shift|", "local|", "declare|", "readonly|", "typeset|", "set|", "unset|", "trap|",
	"eval|", "exec|", "echo|", "printf|", "read|", "cd|", "test|", "true!", "false!", NULL
};

static const char *const SQL_HL_extensions[] = {".sql", NULL};
static const char *const SQL_HL_keywords[] = {
	"SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE",
	"ALTER", "DROP", "TABLE", "DATABASE", "INDEX", "VIEW", "PROCEDURE", "TRIGGER",
	"JOIN", "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "CROSS", "ON", "AS", "USING",
	"GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET", "WITH",
	"UNION", "INTERSECT", "EXCEPT", "ALL", "AND", "OR", "NOT", "IN", "EXISTS",
	"BETWEEN", "LIKE", "IS", "DISTINCT", "CASE", "WHEN", "THEN", "ELSE", "END",
	"PRIMARY", "KEY", "FOREIGN", "REFERENCES", "UNIQUE", "DEFAULT", "CHECK", "BEGIN", "COMMIT",
	"INT|", "INTEGER|", "BIGINT|", "VARCHAR|", "CHAR|", "TEXT|", "DATE|", "TIMESTAMP|",
	"BOOLEAN|", "FLOAT|", "REAL|", "NUMERIC|", "BLOB|",
	"NULL!", "TRUE!", "FALSE!", NULL
};

static const char *const PERL_HL_extensions[] = {".pl", ".pm", NULL};
static const char *const PERL_HL_keywords[] = {
	"if", "elsif", "else", "unless", "while", "until", "for", "foreach",
	"do", "sub", "package", "use", "require", "my", "our", "local", "state",
	"given", "when", "default", "return", "last", "next", "redo", "goto", "eval",
	"BEGIN", "END", "print", "printf", "say", "die", "undef!", NULL
};

static const char *const R_HL_extensions[] = {".r", ".R", NULL};
static const char *const R_HL_keywords[] = {
	"if", "else", "repeat", "while", "function", "for", "in", "next", "break",
	"return", "library", "require", "source",
	"TRUE!", "FALSE!", "NULL!", "Inf!", "NaN!", "NA!", NULL
};

static const char *const ELIXIR_HL_extensions[] = {".ex", ".exs", NULL};
static const char *const ELIXIR_HL_keywords[] = {
	"def", "defp", "defmodule", "defprotocol", "defmacro", "defmacrop",
	"defdelegate", "defexception", "defstruct", "defimpl",
	"do", "end", "when", "and", "or", "not", "in", "if", "unless", "else",
	"case", "cond", "try", "catch", "rescue", "raise", "throw", "after",
	"receive", "fn", "import", "require", "use", "alias", "with",
	"nil!", "true!", "false!", "__MODULE__!", NULL
};

static const char *const HASKELL_HL_extensions[] = {".hs", ".lhs", NULL};
static const char *const HASKELL_HL_keywords[] = {
	"module", "import", "qualified", "as", "hiding", "where", "data",
	"type", "newtype", "class", "instance", "deriving", "let", "in",
	"if", "then", "else", "case", "of", "do", "infixl", "infixr", "infix", "forall",
	"Int|", "Integer|", "Float|", "Double|", "Char|", "String|", "Bool|",
	"Maybe|", "Either|", "IO|", "True!", "False!", "Nothing!", "Just!", NULL
};

static const char *const CLOJURE_HL_extensions[] = {".clj", ".cljs", ".cljc", ".edn", NULL};
static const char *const CLOJURE_HL_keywords[] = {
	"def", "defn", "defn-", "defmacro", "defmulti", "defmethod", "defonce",
	"declare", "ns", "fn", "let", "letfn", "binding", "if", "if-not", "when", "when-not",
	"cond", "case", "and", "or", "not", "do", "doto", "loop", "recur", "for",
	"doseq", "dotimes", "while", "try", "catch", "finally", "throw",
	"nil!", "true!", "false!", NULL
};

static const char *const DART_HL_extensions[] = {".dart", NULL};
static const char *const DART_HL_keywords[] = {
	"abstract", "as", "assert", "async", "await", "break", "case", "catch",
	"class", "const", "continue", "default", "do", "else", "enum", "export",
	"extends", "factory", "final", "finally", "for", "if", "implements", "import",
	"in", "is", "late", "library", "mixin", "new", "required", "return", "static",
	"super", "switch", "this", "throw", "try", "var", "while", "with", "yield",
	"void|", "dynamic|", "int|", "double|", "num|", "String|", "bool|", "List|", "Map|", "Set|",
	"true!", "false!", "null!", NULL
};

static const char *const JULIA_HL_extensions[] = {".jl", NULL};
static const char *const JULIA_HL_keywords[] = {
	"abstract", "baremodule", "begin", "break", "catch", "const", "continue",
	"do", "else", "elseif", "end", "export", "finally", "for",
	"function", "global", "if", "import", "let", "local", "macro", "module",
	"quote", "return", "struct", "try", "using", "while", "where", "in", "isa", "mutable",
	"Int|", "Int64|", "UInt8|", "Float64|", "Bool|", "Char|", "String|", "Any|", "Vector|",
	"true!", "false!", "nothing!", "missing!", "Inf!", "NaN!", NULL
};

static const char *const ZIG_HL_extensions[] = {".zig", NULL};
static const char *const ZIG_HL_keywords[] = {
	"const", "var", "fn", "pub", "extern", "export", "inline", "comptime",
	"if", "else", "switch", "and", "or", "while", "for", "break", "continue",
	"return", "defer", "errdefer", "try", "catch", "orelse", "struct", "union", "enum",
	"error", "packed", "opaque", "test", "usingnamespace", "asm", "volatile",
	"i8|", "u8|", "i16|", "u16|", "i32|", "u32|", "i64|", "u64|", "i128|",
	"u128|", "isize|", "usize|", "f16|", "f32|", "f64|", "bool|", "void|",
	"noreturn|", "type|", "anyerror|", "anytype|", "true!", "false!", "null!", "undefined!", NULL
};

static const char *const NIM_HL_extensions[] = {".nim", ".nims", NULL};
static const char *const NIM_HL_keywords[] = {
	"proc", "func", "method", "iterator", "template", "macro", "converter",
	"var", "let", "const", "type", "object", "tuple", "enum", "ref", "ptr",
	"if", "elif", "else", "when", "case", "of", "while", "for", "in", "break",
	"continue", "return", "yield", "block", "try", "except", "finally",
	"raise", "defer", "import", "include", "from", "export", "as", "and", "or", "not",
	"int|", "float|", "bool|", "char|", "string|", "seq|", "array|", "void|",
	"true!", "false!", "nil!", NULL
};

static const char *const OCAML_HL_extensions[] = {".ml", ".mli", NULL};
static const char *const OCAML_HL_keywords[] = {
	"let", "rec", "in", "fun", "function", "if", "then", "else", "match",
	"with", "when", "try", "raise", "begin", "end", "for", "to", "downto",
	"while", "do", "done", "mutable", "type", "of", "module", "and",
	"struct", "sig", "functor", "val", "open", "include", "exception",
	"int|", "float|", "bool|", "char|", "string|", "unit|", "list|",
	"array|", "option|", "true!", "false!", NULL
};

static const char *const FSHARP_HL_extensions[] = {".fs", ".fsi", ".fsx", NULL};
static const char *const FSHARP_HL_keywords[] = {
	"let", "rec", "mutable", "fun", "function", "if", "then", "else",
	"elif", "match", "with", "when", "try", "finally", "for", "to", "in",
	"downto", "while", "do", "yield", "return", "use", "new",
	"type", "of", "module", "namespace", "open", "member", "val", "inherit",
	"abstract", "interface", "class", "struct", "exception",
	"int|", "float|", "bool|", "char|", "string|", "unit|", "list|",
	"array|", "seq|", "option|", "true!", "false!", "null!", NULL
};

static const char *const ASM_HL_extensions[] = {".asm", ".s", ".S", ".nasm", NULL};
static const char *const ASM_HL_keywords[] = {
	"mov", "movl", "movq", "movb", "movw", "lea", "push", "pop", "add",
	"sub", "mul", "imul", "div", "inc", "dec", "cmp", "jmp", "je", "jne", "jz",
	"jnz", "jg", "jge", "jl", "jle", "call", "ret", "nop", "int", "syscall",
	"and", "or", "xor", "not", "shl", "shr", "sal", "sar", "test",
	"eax|", "ebx|", "ecx|", "edx|", "esi|", "edi|", "esp|", "ebp|",
	"rax|", "rbx|", "rcx|", "rdx|", "rsi|", "rdi|", "rsp|", "rbp|",
	"r8|", "r9|", "r10|", "r11|", "r12|", "r13|", "r14|", "r15|",
	"section", "global", "extern", "db", "dw", "dd", "dq", "resb", "resw", "resd", "resq", NULL
};

static const char *const VIM_HL_extensions[] = {".vim", ".vimrc", NULL};
static const char *const VIM_HL_keywords[] = {
	"if", "else", "elseif", "endif", "for", "endfor", "while", "endwhile",
	"function", "endfunction", "return", "call", "let", "set", "execute",
	"normal", "command", "autocmd", "augroup", "map", "nmap", "vmap",
	"imap", "noremap", "nnoremap", "vnoremap", "inoremap", "syntax",
	"highlight", "colorscheme", "source", "runtime", "echo", "echom", NULL
};

static const char *const HTML_HL_extensions[] = {".html", ".htm", ".xhtml", NULL};
static const char *const XML_HL_extensions[] = {".xml", ".svg", ".xsl", ".xslt", ".plist", NULL};
static const char *const CSS_HL_extensions[] = {".css", NULL};
static const char *const SCSS_HL_extensions[] = {".scss", ".sass", ".less", NULL};
static const char *const CSS_HL_keywords[] = {
	"none!", "auto!", "inherit!", "initial!", "unset!", "important", "solid!", "block!",
	"inline!", "flex!", "grid!", "absolute!", "relative!", "fixed!", "sticky!", "hidden!",
	"transparent!", NULL
};
static const char *const MD_HL_extensions[] = {".md", ".markdown", ".mdown", ".mkd", NULL};
static const char *const YAML_HL_extensions[] = {".yml", ".yaml", NULL};
static const char *const YAML_HL_keywords[] = {
	"true!", "false!", "null!", "True!", "False!", "Null!", "TRUE!", "FALSE!",
	"NULL!", "yes!", "no!", "Yes!", "No!", "on!", "off!", "On!", "Off!", NULL
};
static const char *const JSON_HL_extensions[] = {".json", ".geojson", ".webmanifest", NULL};
static const char *const JSONC_HL_extensions[] = {".jsonc", ".json5", NULL};
static const char *const JSON_HL_keywords[] = {"true!", "false!", "null!", NULL};
static const char *const DOCKER_HL_extensions[] = {"Dockerfile", "Containerfile", ".dockerfile", NULL};
static const char *const DOCKER_HL_keywords[] = {
	"FROM", "RUN", "CMD", "LABEL", "EXPOSE", "ENV", "ADD", "COPY",
	"ENTRYPOINT", "VOLUME", "USER", "WORKDIR", "ARG", "ONBUILD",
	"STOPSIGNAL", "HEALTHCHECK", "SHELL", "MAINTAINER", "AS", NULL
};
static const char *const MAKE_HL_extensions[] = {"Makefile", "makefile", "GNUmakefile", ".mk", ".mak", NULL};
static const char *const MAKE_HL_keywords[] = {
	"ifeq", "ifneq", "ifdef", "ifndef", "else", "endif", "include", "-include",
	"define", "endef", "override", "export", "unexport", "vpath",
	".PHONY", ".SUFFIXES", ".DEFAULT", ".PRECIOUS", ".INTERMEDIATE",
	".SECONDARY", ".DELETE_ON_ERROR", ".SILENT", ".ONESHELL", ".POSIX", NULL
};
static const char *const TOML_HL_extensions[] = {".toml", NULL};
static const char *const INI_HL_extensions[] = {".ini", ".cfg", ".conf", ".editorconfig", ".gitconfig",
	".gitignore", ".gitattributes", ".npmrc", ".env", NULL};
static const char *const BOOL_HL_keywords[] = {"true!", "false!", NULL};
static const char *const HAKE_HL_extensions[] = {".hakorc", "hakorc", NULL};
static const char *const DIFF_HL_extensions[] = {".diff", ".patch", NULL};
static const char *const NONE_HL[] = {NULL};

#define C_LIKE  (HLF_NUMBERS | HLF_STRINGS)
struct editorSyntax HLDB[] = {
	{.filetype = "c", .filematch = C_HL_extensions, .keywords = C_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_PREPROC | HLF_SQCHAR},
	{.filetype = "c++", .filematch = CPP_HL_extensions, .keywords = CPP_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_PREPROC | HLF_SQCHAR},
	{.filetype = "python", .filematch = PY_HL_extensions, .keywords = PY_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_DECOR},
	{.filetype = "javascript", .filematch = JS_HL_extensions, .keywords = JS_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_BACKTICK, .quotes = "\"'`"},
	{.filetype = "typescript", .filematch = TS_HL_extensions, .keywords = TS_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_BACKTICK | HLF_DECOR, .quotes = "\"'`"},
	{.filetype = "java", .filematch = JAVA_HL_extensions, .keywords = JAVA_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_TRIPLE | HLF_DECOR},
	{.filetype = "go", .filematch = GO_HL_extensions, .keywords = GO_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_BACKTICK | HLF_RAWBT, .quotes = "\"'`"},
	{.filetype = "rust", .filematch = RUST_HL_extensions, .keywords = RUST_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_NEST | HLF_BANG | HLF_MLSTR},
	{.filetype = "ruby", .filematch = RUBY_HL_extensions, .keywords = RUBY_HL_keywords, .slc = "#", .mcs = "=begin", .mce = "=end",
	 .flags = C_LIKE | HLF_MLSTR | HLF_DECOR | HLF_MCS_BOL},
	{.filetype = "swift", .filematch = SWIFT_HL_extensions, .keywords = SWIFT_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_NEST | HLF_TRIPLE | HLF_DECOR, .quotes = "\""},
	{.filetype = "php", .filematch = PHP_HL_extensions, .keywords = PHP_HL_keywords, .slc = "//", .slc2 = "#", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_MLSTR | HLF_DOLLAR},
	{.filetype = "kotlin", .filematch = KOTLIN_HL_extensions, .keywords = KOTLIN_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_TRIPLE | HLF_NEST | HLF_DECOR},
	{.filetype = "c#", .filematch = CS_HL_extensions, .keywords = CS_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_PREPROC | HLF_TRIPLE},
	{.filetype = "scala", .filematch = SCALA_HL_extensions, .keywords = SCALA_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_TRIPLE | HLF_NEST | HLF_DECOR},
	{.filetype = "lua", .filematch = LUA_HL_extensions, .keywords = LUA_HL_keywords, .slc = "--",
	 .flags = C_LIKE | HLF_LUALONG},
	{.filetype = "perl", .filematch = PERL_HL_extensions, .keywords = PERL_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_MLSTR | HLF_DOLLAR | HLF_RAWSQ},
	{.filetype = "r", .filematch = R_HL_extensions, .keywords = R_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_MLSTR},
	{.filetype = "shell", .filematch = SHELL_HL_extensions, .keywords = SHELL_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_MLSTR | HLF_RAWSQ | HLF_DOLLAR | HLF_HASHWORD},
	{.filetype = "sql", .filematch = SQL_HL_extensions, .keywords = SQL_HL_keywords, .slc = "--", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_NOCASE | HLF_RAWSQ | HLF_MLSTR},
	{.filetype = "html", .filematch = HTML_HL_extensions, .keywords = NONE_HL, .mode = HLM_MARKUP},
	{.filetype = "xml", .filematch = XML_HL_extensions, .keywords = NONE_HL, .mode = HLM_MARKUP},
	{.filetype = "css", .filematch = CSS_HL_extensions, .keywords = CSS_HL_keywords, .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_DASHID, .mode = HLM_CSS},
	{.filetype = "scss", .filematch = SCSS_HL_extensions, .keywords = CSS_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_DASHID | HLF_DOLLAR | HLF_HASHWORD, .mode = HLM_CSS},
	{.filetype = "markdown", .filematch = MD_HL_extensions, .keywords = NONE_HL, .mode = HLM_MARKDOWN},
	{.filetype = "yaml", .filematch = YAML_HL_extensions, .keywords = YAML_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_HASHWORD | HLF_RAWSQ, .mode = HLM_YAML},
	{.filetype = "json", .filematch = JSON_HL_extensions, .keywords = JSON_HL_keywords,
	 .flags = C_LIKE | HLF_JSONKEY, .quotes = "\""},
	{.filetype = "jsonc", .filematch = JSONC_HL_extensions, .keywords = JSON_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_JSONKEY, .quotes = "\""},
	{.filetype = "toml", .filematch = TOML_HL_extensions, .keywords = BOOL_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_RAWSQ, .mode = HLM_INI},
	{.filetype = "ini", .filematch = INI_HL_extensions, .keywords = BOOL_HL_keywords, .slc = "#", .slc2 = ";",
	 .flags = HLF_STRINGS | HLF_SLC_BOL, .mode = HLM_INI, .quotes = "\""},
	{.filetype = "docker", .filematch = DOCKER_HL_extensions, .keywords = DOCKER_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_NOCASE | HLF_DOLLAR | HLF_SLC_BOL},
	{.filetype = "makefile", .filematch = MAKE_HL_extensions, .keywords = MAKE_HL_keywords, .slc = "#",
	 .flags = HLF_STRINGS | HLF_DOLLAR, .mode = HLM_MAKE},
	{.filetype = "diff", .filematch = DIFF_HL_extensions, .keywords = NONE_HL, .mode = HLM_DIFF},
	{.filetype = "vim", .filematch = VIM_HL_extensions, .keywords = VIM_HL_keywords, .slc = "\"",
	 .flags = C_LIKE | HLF_SLC_BOL},
	{.filetype = "elixir", .filematch = ELIXIR_HL_extensions, .keywords = ELIXIR_HL_keywords, .slc = "#",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_MLSTR | HLF_DECOR},
	{.filetype = "haskell", .filematch = HASKELL_HL_extensions, .keywords = HASKELL_HL_keywords, .slc = "--", .mcs = "{-", .mce = "-}",
	 .flags = C_LIKE | HLF_NEST | HLF_SQCHAR},
	{.filetype = "clojure", .filematch = CLOJURE_HL_extensions, .keywords = CLOJURE_HL_keywords, .slc = ";",
	 .flags = C_LIKE | HLF_DASHID | HLF_MLSTR, .quotes = "\""},
	{.filetype = "dart", .filematch = DART_HL_extensions, .keywords = DART_HL_keywords, .slc = "//", .mcs = "/*", .mce = "*/",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_DECOR | HLF_DOLLAR},
	{.filetype = "julia", .filematch = JULIA_HL_extensions, .keywords = JULIA_HL_keywords, .slc = "#", .mcs = "#=", .mce = "=#",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_SQCHAR | HLF_NEST},
	{.filetype = "zig", .filematch = ZIG_HL_extensions, .keywords = ZIG_HL_keywords, .slc = "//",
	 .flags = C_LIKE | HLF_SQCHAR | HLF_DECOR},
	{.filetype = "nim", .filematch = NIM_HL_extensions, .keywords = NIM_HL_keywords, .slc = "#", .mcs = "#[", .mce = "]#",
	 .flags = C_LIKE | HLF_TRIPLE | HLF_SQCHAR | HLF_NEST},
	{.filetype = "ocaml", .filematch = OCAML_HL_extensions, .keywords = OCAML_HL_keywords, .mcs = "(*", .mce = "*)",
	 .flags = C_LIKE | HLF_NEST | HLF_SQCHAR},
	{.filetype = "f#", .filematch = FSHARP_HL_extensions, .keywords = FSHARP_HL_keywords, .slc = "//", .mcs = "(*", .mce = "*)",
	 .flags = C_LIKE | HLF_NEST | HLF_SQCHAR | HLF_TRIPLE},
	{.filetype = "assembly", .filematch = ASM_HL_extensions, .keywords = ASM_HL_keywords, .slc = ";", .slc2 = "#",
	 .flags = C_LIKE},
	{.filetype = "hakorc", .filematch = HAKE_HL_extensions, .keywords = BOOL_HL_keywords, .slc = "#",
	 .flags = HLF_SLC_BOL | HLF_NUMBERS, .mode = HLM_INI, .quotes = ""},
	{.filetype = "text", .filematch = NONE_HL, .keywords = NONE_HL},
};
#define HLDB_ENTRIES ((int)(sizeof(HLDB) / sizeof(HLDB[0])))
#define HL_TEXT (&HLDB[HLDB_ENTRIES - 1])

static int hkIdentChar(const struct editorSyntax *syn, unsigned char c) {
	return isalnum(c) || c == '_' || c >= 0x80 || ((syn->flags & HLF_DASHID) && (c == '-' || c == '?' || c == '!'));
}

/* 0, or the class of the keyword s[0..len) */
static int hkKeyword(const struct editorSyntax *syn, const char *s, int len) {
	const char *const *kw = syn->keywords;
	if (!kw) return 0;
	int nocase = syn->flags & HLF_NOCASE;
	for (int j = 0; kw[j]; j++) {
		const char *k = kw[j];
		int kl = (int)strlen(k);
		int cls = HL_KEYWORD1;
		if (kl > 1 && k[kl - 1] == '|') { kl--; cls = HL_KEYWORD2; }
		else if (kl > 1 && k[kl - 1] == '!') { kl--; cls = HL_CONSTANT; }
		if (kl != len) continue;
		if (nocase ? strncasecmp(k, s, len) == 0 : (k[0] == s[0] && strncmp(k, s, len) == 0)) return cls;
	}
	return 0;
}

static void hkMark(unsigned char *hl, int from, int to, int cls) {
	for (int k = from; k < to; k++) hl[k] = (unsigned char)cls;
}

/* length of the escape at s[i] (s[i] == '\\'), at most to n */
static int hkEscapeLen(const char *s, int i, int n) {
	if (i + 1 >= n) return 1;
	char c = s[i + 1];
	int j = i + 2;
	if (c == 'x') { while (j < n && j < i + 4 && isxdigit((unsigned char)s[j])) j++; }
	else if (c == 'u' && j < n && s[j] == '{') { while (j < n && s[j] != '}') j++; if (j < n) j++; }
	else if (c == 'u') { while (j < n && j < i + 6 && isxdigit((unsigned char)s[j])) j++; }
	else if (c == 'U') { while (j < n && j < i + 10 && isxdigit((unsigned char)s[j])) j++; }
	else if (c >= '0' && c <= '7') { while (j < n && j < i + 4 && s[j] >= '0' && s[j] <= '7') j++; }
	return j - i;
}

/* Inside a string: marks up to the closing delimiter. Returns the index after
   it, and sets *closed. raw: no escapes. */
static int hkScanString(const char *s, int i, int n, unsigned char *hl, const char *close, int raw, int *closed, int vars) {
	int cl = (int)strlen(close);
	*closed = 0;
	while (i < n) {
		if (vars && s[i] == '$' && i + 1 < n && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_' || s[i + 1] == '{')) {
			int j = i + 1;                          /* "$HOME and ${PATH}" */
			if (s[j] == '{') { while (j < n && s[j] != '}') j++; if (j < n) j++; }
			else while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
			hkMark(hl, i, j, HL_VARIABLE);
			i = j;
			continue;
		}
		if (!raw && s[i] == '\\') {
			int e = hkEscapeLen(s, i, n);
			hkMark(hl, i, i + e, HL_ESCAPE);
			i += e;
			continue;
		}
		if (i + cl <= n && strncmp(s + i, close, cl) == 0) {
			hkMark(hl, i, i + cl, HL_STRING);
			*closed = 1;
			return i + cl;
		}
		hl[i++] = HL_STRING;
	}
	return i;
}

/* the number at s[i]: 0x1F, 1_000, 1e-5, 2.5f, 10u32, .5 */
static int hkScanNumber(const char *s, int i, int n) {
	int hex = (s[i] == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X'));
	int j = i;
	while (j < n) {
		unsigned char c = (unsigned char)s[j];
		if (isalnum(c) || c == '_') { j++; continue; }
		if (c == '.' && j + 1 < n && isdigit((unsigned char)s[j + 1])) { j++; continue; }
		if (c == '.' && j > i && (j + 1 >= n || (!isalpha((unsigned char)s[j + 1]) && s[j + 1] != '.' && s[j + 1] != '_'))) { j++; continue; }
		if ((c == '+' || c == '-') && j > i && !hex && (s[j - 1] == 'e' || s[j - 1] == 'E')) { j++; continue; }
		if ((c == '+' || c == '-') && j > i && hex && (s[j - 1] == 'p' || s[j - 1] == 'P')) { j++; continue; }
		break;
	}
	return j;
}

static int hkLuaLong(const char *s, int i, int n, int *level) {
	if (i >= n || s[i] != '[') return 0;
	int j = i + 1, l = 0;
	while (j < n && s[j] == '=') { j++; l++; }
	if (j < n && s[j] == '[') { *level = l; return j + 1 - i; }
	return 0;
}

static int hkLuaClose(const char *s, int i, int n, int level) {
	if (s[i] != ']') return 0;
	int j = i + 1, l = 0;
	while (j < n && s[j] == '=') { j++; l++; }
	return (j < n && s[j] == ']' && l == level) ? j + 1 - i : 0;
}

/* The generic highlighter: one line, in-state st, returns the out-state. */
static int hkHlCode(const struct editorSyntax *syn, const char *s, int n, unsigned char *hl, int st) {
	int kind = ST_KIND(st), lvl = ST_LVL(st), hi = ST_HI(st);
	const char *quotes = syn->quotes ? syn->quotes : "\"'";
	int f = syn->flags;
	int scs = syn->slc ? (int)strlen(syn->slc) : 0;
	int scs2 = syn->slc2 ? (int)strlen(syn->slc2) : 0;
	int mcs = syn->mcs ? (int)strlen(syn->mcs) : 0;
	int mce = syn->mce ? (int)strlen(syn->mce) : 0;
	int css = syn->mode == HLM_CSS;
	int i = 0, bol = 1;
	char qbuf[4];

	while (i < n) {
		/* carried over from the line above */
		if (kind == ST_COMMENT) {
			while (i < n) {
				if (mce && i + mce <= n && !strncmp(s + i, syn->mce, mce)) {
					hkMark(hl, i, i + mce, HL_MLCOMMENT);
					i += mce;
					if (lvl > 0 && (f & HLF_NEST)) { lvl--; continue; }
					kind = ST_NONE; lvl = 0;
					break;
				}
				if ((f & HLF_NEST) && mcs && i + mcs <= n && !strncmp(s + i, syn->mcs, mcs)) {
					hkMark(hl, i, i + mcs, HL_MLCOMMENT);
					i += mcs;
					if (lvl < 15) lvl++;
					continue;
				}
				hl[i++] = HL_MLCOMMENT;
			}
			continue;
		}
		if (kind == ST_LUACOM || kind == ST_LUASTR) {
			int cls = kind == ST_LUACOM ? HL_MLCOMMENT : HL_STRING;
			while (i < n) {
				int c = hkLuaClose(s, i, n, lvl);
				if (c) { hkMark(hl, i, i + c, cls); i += c; kind = ST_NONE; lvl = 0; break; }
				hl[i++] = (unsigned char)cls;
			}
			continue;
		}
		if (kind == ST_TDQ || kind == ST_TSQ || kind == ST_BT || kind == ST_DQ || kind == ST_SQ) {
			const char *close = kind == ST_TDQ ? "\"\"\"" : kind == ST_TSQ ? "'''" : kind == ST_BT ? "`" : kind == ST_DQ ? "\"" : "'";
			int raw = (kind == ST_BT && (f & HLF_RAWBT)) || ((kind == ST_SQ || kind == ST_TSQ) && (f & HLF_RAWSQ));
			int closed, start = i;
			i = hkScanString(s, i, n, hl, close, raw, &closed, (f & HLF_DOLLAR) && kind != ST_SQ && kind != ST_TSQ);
			if (closed) {
				if ((f & HLF_JSONKEY) && kind == ST_DQ) {
					int k = i, ks = (start > 0 && s[start - 1] == '"') ? start - 1 : start;
					while (k < n && (s[k] == ' ' || s[k] == '\t')) k++;
					if (k < n && s[k] == ':') hkMark(hl, ks, i, HL_KEY);
				}
				kind = ST_NONE;
			} else if ((kind == ST_DQ || kind == ST_SQ) && !(f & HLF_MLSTR) && !(n > 0 && s[n - 1] == '\\')) {
				kind = ST_NONE;                     /* unterminated: it ends with the line */
			}
			continue;
		}

		unsigned char c = (unsigned char)s[i];
		int prev_ident = i > 0 && hkIdentChar(syn, (unsigned char)s[i - 1]);
		if (c == ' ' || c == '\t') { i++; continue; }

		/* comments */
		int word_ok = !(f & HLF_HASHWORD) || i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t';
		int bol_ok = !(f & HLF_SLC_BOL) || bol;
		if ((f & HLF_LUALONG) && scs && i + 2 < n && !strncmp(s + i, "--", 2)) {
			int l = 0, len = hkLuaLong(s, i + 2, n, &l);
			if (len) { hkMark(hl, i, i + 2 + len, HL_MLCOMMENT); i += 2 + len; kind = ST_LUACOM; lvl = l; continue; }
		}
		if (word_ok && bol_ok && ((scs && i + scs <= n && !strncmp(s + i, syn->slc, scs)) ||
		                          (scs2 && i + scs2 <= n && !strncmp(s + i, syn->slc2, scs2)))) {
			hkMark(hl, i, n, HL_COMMENT);
			break;
		}
		if (mcs && i + mcs <= n && !strncmp(s + i, syn->mcs, mcs) && (!(f & HLF_MCS_BOL) || i == 0)) {
			hkMark(hl, i, i + mcs, HL_MLCOMMENT);
			i += mcs;
			kind = ST_COMMENT; lvl = 0;
			continue;
		}
		if ((f & HLF_LUALONG) && c == '[') {
			int l = 0, len = hkLuaLong(s, i, n, &l);
			if (len) { hkMark(hl, i, i + len, HL_STRING); i += len; kind = ST_LUASTR; lvl = l; continue; }
		}

		/* # directives */
		if ((f & HLF_PREPROC) && c == '#' && bol) {
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t')) j++;
			int w = j;
			while (j < n && isalpha((unsigned char)s[j])) j++;
			hkMark(hl, i, j, HL_PREPROCESSOR);
			int inc = (j - w == 7 && !strncmp(s + w, "include", 7)) || (j - w == 6 && !strncmp(s + w, "import", 6));
			i = j;
			bol = 0;
			if (inc) {
				while (i < n && s[i] == ' ') i++;
				if (i < n && s[i] == '<') {
					int e = i;
					while (e < n && s[e] != '>') e++;
					hkMark(hl, i, e < n ? e + 1 : n, HL_STRING);
					i = e < n ? e + 1 : n;
				}
			}
			continue;
		}
		bol = 0;
		if ((f & HLF_BANG) && c == '#' && i + 1 < n && (s[i + 1] == '[' || (s[i + 1] == '!' && i + 2 < n && s[i + 2] == '['))) {
			int j = i, depth = 0;
			while (j < n) { if (s[j] == '[') depth++; else if (s[j] == ']' && --depth == 0) { j++; break; } j++; }
			hkMark(hl, i, j, HL_PREPROCESSOR);
			i = j;
			continue;
		}
		if ((f & HLF_DECOR) && c == '@' && i + 1 < n && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_' || s[i + 1] == '@')) {
			int j = i + 1;
			while (j < n && (hkIdentChar(syn, (unsigned char)s[j]) || s[j] == '.' || s[j] == '@')) j++;
			hkMark(hl, i, j, HL_PREPROCESSOR);
			i = j;
			continue;
		}
		if ((f & HLF_DOLLAR) && c == '$' && i + 1 < n) {
			int j = i + 1;
			char o = s[j];
			if (o == '{' || (o == '(' && syn->mode == HLM_MAKE)) {
				char cl = o == '{' ? '}' : ')';
				int depth = 0;
				while (j < n) { if (s[j] == o) depth++; else if (s[j] == cl && --depth == 0) { j++; break; } j++; }
			} else if (o == '(') {
				j++;                                /* $( in a shell is a command: only mark the opener */
			} else if (isalpha((unsigned char)o) || o == '_') {
				while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
			} else if (strchr("0123456789@#?*!$-<^%+", o)) {
				j++;
			} else {
				hl[i++] = HL_OPERATOR;
				continue;
			}
			hkMark(hl, i, j, HL_VARIABLE);
			i = j;
			continue;
		}

		/* strings */
		if ((f & HLF_STRINGS) && c && strchr(quotes, c)) {
			if ((f & HLF_TRIPLE) && (c == '"' || c == '\'') && i + 2 < n && s[i + 1] == (char)c && s[i + 2] == (char)c) {
				hkMark(hl, i, i + 3, HL_STRING);
				i += 3;
				kind = c == '"' ? ST_TDQ : ST_TSQ;
				continue;
			}
			if (c == '\'' && (f & HLF_SQCHAR)) {
				if (prev_ident) { i++; continue; }  /* x' (a prime), it's */
				int j = i + 1;
				if (j < n && s[j] == '\\') j += hkEscapeLen(s, j, n);
				else if (j < n) j += utf8_byte_length((unsigned char)s[j]);
				if (j < n && s[j] == '\'') {
					hkMark(hl, i, j + 1, HL_CHAR);
					for (int k = i + 1; k < j; k++) if (s[k] == '\\') { hkMark(hl, k, j, HL_ESCAPE); break; }
					i = j + 1;
					continue;
				}
				j = i + 1;                          /* 'a lifetime, a label */
				while (j < n && hkIdentChar(syn, (unsigned char)s[j])) j++;
				hkMark(hl, i, j, j > i + 1 ? HL_LABEL : HL_OPERATOR);
				i = j;
				continue;
			}
			if (syn->mode == HLM_YAML) {             /* a quote opens a scalar, not a word */
				int k = i - 1;
				while (k >= 0 && s[k] == ' ') k--;
				if (k >= 0 && !strchr(":-[{,?", s[k])) { i++; continue; }
			}
			if (c == '`' && (f & HLF_BACKTICK)) {
				hl[i++] = HL_STRING;
				kind = ST_BT;
				continue;
			}
			qbuf[0] = (char)c; qbuf[1] = '\0';
			hl[i++] = HL_STRING;
			kind = c == '"' ? ST_DQ : c == '\'' ? ST_SQ : ST_BT;
			if (kind == ST_BT && !(f & HLF_BACKTICK)) {      /* a ` string that may not span lines */
				int closed;
				i = hkScanString(s, i, n, hl, qbuf, 0, &closed, 0);
				kind = ST_NONE;
			}
			continue;
		}

		/* numbers */
		if ((f & HLF_NUMBERS) && !prev_ident && (isdigit(c) || (c == '.' && i + 1 < n && isdigit((unsigned char)s[i + 1])))) {
			int j = hkScanNumber(s, i, n);
			hkMark(hl, i, j, HL_NUMBER);
			i = j;
			continue;
		}
		if (css && c == '#' && hi > 0) {             /* a color value */
			int j = i + 1;
			while (j < n && isxdigit((unsigned char)s[j])) j++;
			hkMark(hl, i, j, HL_NUMBER);
			i = j;
			continue;
		}

		/* names */
		if (isalpha(c) || c == '_' || c >= 0x80 || ((f & HLF_DASHID) && c == '-' && i + 1 < n && isalpha((unsigned char)s[i + 1]))) {
			int j = i;
			while (j < n && hkIdentChar(syn, (unsigned char)s[j])) j++;
			int len = j - i;
			if ((f & HLF_STRINGS) && j < n && strchr(quotes, s[j]) && s[j] && len <= 2) {
				int pfx = 1;                        /* r"..", b'..', f"..", u8"..", L".." */
				for (int k = i; k < j; k++) if (!strchr("rRbBfFuUlL8", s[k])) pfx = 0;
				if (pfx && syn->mode != HLM_YAML) { hkMark(hl, i, j, HL_STRING); i = j; continue; }
			}
			if ((f & HLF_BANG) && j < n && s[j] == '!' && (j + 1 >= n || s[j + 1] != '=')) {
				hkMark(hl, i, j + 1, HL_PREPROCESSOR);
				i = j + 1;
				continue;
			}
			if (css) {
				int k = j;
				while (k < n && s[k] == ' ') k++;
				if (hi > 0 && k < n && s[k] == ':') { hkMark(hl, i, j, HL_KEY); i = j; continue; }
				if (hi == 0) {
					int cls = (i > 0 && s[i - 1] == ':') ? HL_KEYWORD2 : HL_KEYWORD1;
					if (i > 0 && (s[i - 1] == '.' || s[i - 1] == '#')) hl[i - 1] = (unsigned char)cls;
					if (i > 0 && s[i - 1] == '@') { hl[i - 1] = HL_PREPROCESSOR; cls = HL_PREPROCESSOR; }
					hkMark(hl, i, j, cls);
					i = j;
					continue;
				}
			}
			int cls = hkKeyword(syn, s + i, len);
			if (cls) hkMark(hl, i, j, cls);
			i = j;
			continue;
		}

		if (css && c == '{') hi = hi < 255 ? hi + 1 : hi;
		if (css && c == '}') hi = hi > 0 ? hi - 1 : 0;
		if (strchr("(){}[]", c)) hl[i] = HL_BRACKET;
		else if (strchr("+-*/%=<>!&|^~?:,;.", c)) hl[i] = HL_OPERATOR;
		i++;
	}
	if (kind == ST_DQ && !(f & HLF_MLSTR) && !(n > 0 && s[n - 1] == '\\')) kind = ST_NONE;
	if (kind == ST_SQ && !(f & HLF_MLSTR) && !(n > 0 && s[n - 1] == '\\')) kind = ST_NONE;
	return ST_MAKE(kind, lvl, hi);
}

static int hkBlank(const char *s, int n) {
	for (int i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t') return 0;
	return 1;
}

/* key = value, [section], # comments at the start of a line */
static void hkHlIni(const char *s, int n, unsigned char *hl) {
	int i = 0;
	while (i < n && s[i] == ' ') i++;
	if (i < n && s[i] == '[') {
		int e = n;
		while (e > i && s[e - 1] != ']') e--;
		if (e > i) hkMark(hl, i, e, HL_KEYWORD1);
		return;
	}
	int k = i;
	while (k < n && s[k] != '=' && s[k] != ':' && s[k] != '"' && s[k] != '#') k++;
	if (k < n && k > i && (s[k] == '=' || s[k] == ':') && hl[i] != HL_COMMENT) {
		int e = k;
		while (e > i && s[e - 1] == ' ') e--;
		hkMark(hl, i, e, HL_KEY);
	}
}

static void hkHlYaml(const char *s, int n, unsigned char *hl) {
	if (n >= 3 && (!strncmp(s, "---", 3) || !strncmp(s, "...", 3)) && hkBlank(s + 3, n - 3)) {
		hkMark(hl, 0, n, HL_PREPROCESSOR);
		return;
	}
	int i = 0;
	while (i < n && (s[i] == ' ' || s[i] == '-')) {
		if (s[i] == '-' && (i + 1 >= n || s[i + 1] == ' ')) hl[i] = HL_MARKER;
		i++;
	}
	int k = i;
	if (k < n && (s[k] == '"' || s[k] == '\'')) {        /* "quoted key": */
		char q = s[k++];
		while (k < n && s[k] != q) k++;
		if (k < n) k++;
	} else {
		while (k < n && s[k] != ':' && s[k] != '#') k++;
	}
	if (k < n && s[k] == ':' && (k + 1 >= n || s[k + 1] == ' ') && k > i && hl[i] != HL_COMMENT)
		hkMark(hl, i, k, HL_KEY);
	for (int j = k; j < n; j++) {
		if (hl[j] != HL_NORMAL && hl[j] != HL_OPERATOR) continue;
		if ((s[j] == '&' || s[j] == '*') && (j == 0 || s[j - 1] == ' ') && j + 1 < n && s[j + 1] != ' ') {
			int e = j + 1;
			while (e < n && s[e] != ' ') e++;
			hkMark(hl, j, e, HL_LABEL);
			j = e;
		} else if (s[j] == '!' && (j == 0 || s[j - 1] == ' ')) {
			int e = j + 1;
			while (e < n && s[e] != ' ') e++;
			hkMark(hl, j, e, HL_PREPROCESSOR);
			j = e;
		}
	}
}

/* targets, variable assignments; the rest is the generic pass */
static void hkHlMake(const char *s, int n, unsigned char *hl) {
	if (n == 0 || s[0] == ' ' || s[0] == '\t' || hl[0] == HL_COMMENT) return;
	int k = 0;
	while (k < n && s[k] != ':' && s[k] != '=' && s[k] != '#') k++;
	if (k >= n || s[k] == '#') return;
	int assign = s[k] == '=' || (k + 1 < n && s[k] == ':' && (s[k + 1] == '=' || s[k + 1] == ':'));
	int e = k;
	while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '?' || s[e - 1] == '+' || s[e - 1] == '!')) e--;
	for (int j = 0; j < e; j++)
		if (hl[j] == HL_NORMAL || hl[j] == HL_OPERATOR) hl[j] = (unsigned char)(assign ? HL_KEY : HL_KEYWORD1);
}

static int hkHlDiff(const char *s, int n, unsigned char *hl) {
	if (n == 0) return 0;
	int cls = HL_NORMAL;
	if (!strncmp(s, "+++", 3) || !strncmp(s, "---", 3) || !strncmp(s, "diff ", 5) || !strncmp(s, "index ", 6))
		cls = HL_KEYWORD2;
	else if (!strncmp(s, "@@", 2)) cls = HL_PREPROCESSOR;
	else if (s[0] == '+') cls = HL_DIFF_ADD;
	else if (s[0] == '-') cls = HL_DIFF_DEL;
	else if (s[0] == '\\') cls = HL_COMMENT;
	hkMark(hl, 0, n, cls);
	return 0;
}

/* HTML and XML: tags, attributes, entities, comments; a tag may span lines */
static int hkHlMarkup(const char *s, int n, unsigned char *hl, int st) {
	int kind = ST_KIND(st);
	int i = 0;
	while (i < n) {
		if (kind == ST_COMMENT) {
			while (i < n) {
				if (i + 3 <= n && !strncmp(s + i, "-->", 3)) { hkMark(hl, i, i + 3, HL_MLCOMMENT); i += 3; kind = ST_NONE; break; }
				hl[i++] = HL_MLCOMMENT;
			}
			continue;
		}
		if (kind == ST_TAGDQ || kind == ST_TAGSQ) {
			char q = kind == ST_TAGDQ ? '"' : '\'';
			while (i < n && s[i] != q) hl[i++] = HL_STRING;
			if (i < n) { hl[i++] = HL_STRING; kind = ST_TAG; }
			continue;
		}
		if (kind == ST_TAG) {
			unsigned char c = (unsigned char)s[i];
			if (c == '>' || (c == '/' && i + 1 < n && s[i + 1] == '>') || (c == '?' && i + 1 < n && s[i + 1] == '>')) {
				int e = c == '>' ? i + 1 : i + 2;
				hkMark(hl, i, e, HL_TAG);
				i = e;
				kind = ST_NONE;
			} else if (c == '"' || c == '\'') {
				hl[i++] = HL_STRING;
				kind = c == '"' ? ST_TAGDQ : ST_TAGSQ;
			} else if (isalpha(c) || c == '_' || c == ':' || c == '@') {
				int j = i;
				while (j < n && (isalnum((unsigned char)s[j]) || strchr("_-:.@", s[j]))) j++;
				hkMark(hl, i, j, HL_ATTRNAME);
				i = j;
			} else {
				if (c == '=') hl[i] = HL_OPERATOR;
				i++;
			}
			continue;
		}
		if (i + 4 <= n && !strncmp(s + i, "<!--", 4)) { hkMark(hl, i, i + 4, HL_MLCOMMENT); i += 4; kind = ST_COMMENT; continue; }
		if (s[i] == '<' && i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '?')) {   /* <!DOCTYPE>, <?xml ?> */
			int j = i + 2;
			while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '-')) j++;
			hkMark(hl, i, j, HL_PREPROCESSOR);
			i = j;
			kind = ST_TAG;
			continue;
		}
		if (s[i] == '<' && i + 1 < n && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/')) {
			int j = i + 1;
			if (s[j] == '/') j++;
			while (j < n && (isalnum((unsigned char)s[j]) || strchr("_-:.", s[j]))) j++;
			hkMark(hl, i, j, HL_TAG);
			i = j;
			kind = ST_TAG;
			continue;
		}
		if (s[i] == '&') {
			int j = i + 1;
			while (j < n && j < i + 12 && (isalnum((unsigned char)s[j]) || s[j] == '#')) j++;
			if (j < n && s[j] == ';' && j > i + 1) { hkMark(hl, i, j + 1, HL_ESCAPE); i = j + 1; continue; }
		}
		i++;
	}
	return ST_MAKE(kind, 0, 0);
}

/* ---- markdown ----
   in-state bits: 0-15 the fenced language's own state (or 1 = inside <!-- -->),
   16 in a fence, 17 the fence is ~, 18-21 its length, 22-28 the language + 1,
   29 front matter, 30 in a table. */
#define MD_FENCE  (1 << 16)
#define MD_TILDE  (1 << 17)
#define MD_FLEN(st)  (((st) >> 18) & 15)
#define MD_LANG(st)  (((st) >> 22) & 127)
#define MD_FM     (1 << 29)
#define MD_TABLE  (1 << 30)

static const struct editorSyntax *hkSyntaxByName(const char *name);
static int hkHlLine(const struct editorSyntax *syn, const char *s, int n, unsigned char *hl, int st,
                    const char *prev, int prevn, const char *next, int nextn);

static int hkIndent(const char *s, int n) {
	int i = 0;
	while (i < n && s[i] == ' ') i++;
	return i;
}

/* a run of 3+ ` or ~ after at most 3 spaces: returns its length, sets *ch */
static int hkMdFence(const char *s, int n, char *ch) {
	int i = hkIndent(s, n);
	if (i > 3 || i >= n || (s[i] != '`' && s[i] != '~')) return 0;
	char c = s[i];
	int j = i;
	while (j < n && s[j] == c) j++;
	if (j - i < 3) return 0;
	if (c == '`') for (int k = j; k < n; k++) if (s[k] == '`') return 0;
	*ch = c;
	return j - i;
}

static int hkMdRule(const char *s, int n) {
	int i = hkIndent(s, n), cnt = 0;
	if (i > 3 || i >= n || !strchr("-*_", s[i])) return 0;
	char c = s[i];
	for (; i < n; i++) {
		if (s[i] == c) cnt++;
		else if (s[i] != ' ' && s[i] != '\t') return 0;
	}
	return cnt >= 3;
}

/* === or --- under a line: a setext heading */
static int hkMdUnderline(const char *s, int n) {
	int i = hkIndent(s, n);
	if (i > 3 || i >= n || (s[i] != '=' && s[i] != '-')) return 0;
	char c = s[i];
	while (i < n && s[i] == c) i++;
	return hkBlank(s + i, n - i);
}

static int hkMdTableDelim(const char *s, int n) {
	int dash = 0, pipe = 0;
	if (!s) return 0;
	for (int i = 0; i < n; i++) {
		if (s[i] == '-') dash++;
		else if (s[i] == '|') pipe++;
		else if (s[i] != ':' && s[i] != ' ' && s[i] != '\t') return 0;
	}
	return dash >= 1 && (pipe >= 1 || dash >= 3) && pipe >= 1;
}

static int hkMdListMarker(const char *s, int n, int i) {
	if (i < n && strchr("-*+", s[i]) && (i + 1 >= n || s[i + 1] == ' ' || s[i + 1] == '\t')) return 1;
	int j = i;
	while (j < n && j < i + 9 && isdigit((unsigned char)s[j])) j++;
	if (j > i && j < n && (s[j] == '.' || s[j] == ')') && (j + 1 >= n || s[j + 1] == ' ')) return j + 1 - i;
	return 0;
}

static int hkMdParagraph(const char *s, int n) {
	if (!s || hkBlank(s, n)) return 0;
	int i = hkIndent(s, n);
	char ch;
	if (i < n && (s[i] == '#' || s[i] == '>' || hkMdListMarker(s, n, i) || hkMdFence(s, n, &ch) || hkMdRule(s, n))) return 0;
	return 1;
}

/* inline: code spans, emphasis, links, autolinks, escapes, html comments */
static int hkMdInline(const char *s, int from, int n, unsigned char *hl, int base) {
	int closers[16], nc = 0;
	int i = from;
	while (i < n) {
		for (int k = 0; k < nc; k++) {
			if (closers[k] == i) {
				char ch = s[i];
				while (i < n && s[i] == ch) i++;
				closers[k] = -1;
				goto next;
			}
		}
		unsigned char c = (unsigned char)s[i];
		if (c == '\\' && i + 1 < n && ispunct((unsigned char)s[i + 1])) {
			hl[i] = HL_DELIM;
			i += 2;
			continue;
		}
		if (c == '`') {
			int k = i;
			while (k < n && s[k] == '`') k++;
			int run = k - i, j = k;
			while (j < n) {
				if (s[j] == '`') {
					int e = j;
					while (e < n && s[e] == '`') e++;
					if (e - j == run) { hkMark(hl, i, e, HL_CODE); i = e; goto next; }
					j = e;
				} else j++;
			}
			i = k;
			continue;
		}
		if (c == '<' && i + 4 <= n && !strncmp(s + i, "<!--", 4)) {
			const char *e = NULL;
			for (int j = i + 4; j + 3 <= n; j++) if (!strncmp(s + j, "-->", 3)) { e = s + j; break; }
			int end = e ? (int)(e - s) + 3 : n;
			hkMark(hl, i, end, HL_COMMENT);
			if (!e) return 1;
			i = end;
			continue;
		}
		if (c == '<' && i + 1 < n && (isalpha((unsigned char)s[i + 1]))) {
			int j = i + 1;
			while (j < n && s[j] != '>' && s[j] != ' ') j++;
			if (j < n && s[j] == '>' && memchr(s + i, ':', j - i)) { hkMark(hl, i, j + 1, HL_URL); i = j + 1; continue; }
		}
		if ((c == 'h' && i + 8 <= n && (!strncmp(s + i, "https://", 8) || !strncmp(s + i, "http://", 7))) && (i == 0 || !isalnum((unsigned char)s[i - 1]))) {
			int j = i;
			while (j < n && s[j] != ' ' && s[j] != '\t' && s[j] != '<' && s[j] != '>') j++;
			while (j > i && strchr(".,;:!?)'\"*_", s[j - 1])) j--;
			hkMark(hl, i, j, HL_URL);
			i = j;
			continue;
		}
		if (c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) {
			int o = c == '!' ? i + 1 : i;
			int depth = 0, j = o;
			for (; j < n; j++) {
				if (s[j] == '\\') { j++; continue; }
				if (s[j] == '[') depth++;
				else if (s[j] == ']' && --depth == 0) break;
			}
			if (j < n) {
				if (j + 1 < n && (s[j + 1] == '(' || s[j + 1] == '[')) {
					char cl = s[j + 1] == '(' ? ')' : ']';
					int e = j + 2, d2 = 1;
					while (e < n) { if (s[e] == s[j + 1]) d2++; else if (s[e] == cl && --d2 == 0) break; e++; }
					if (e < n) {
						hkMark(hl, i, j + 1, HL_LINK);
						hkMark(hl, j + 1, e + 1, HL_URL);
						i = e + 1;
						continue;
					}
				}
				if (j + 1 < n && s[j + 1] == ':' && o == hkIndent(s, n)) {        /* [ref]: url */
					hkMark(hl, i, j + 2, HL_LINK);
					hkMark(hl, j + 2, n, HL_URL);
					return 0;
				}
				if (o + 1 < n && s[o + 1] == '^') { hkMark(hl, i, j + 1, HL_LINK); i = j + 1; continue; }
			}
		}
		if (c == '*' || c == '_' || (c == '~' && i + 1 < n && s[i + 1] == '~')) {
			int k = i;
			while (k < n && s[k] == (char)c) k++;
			int run = k - i;
			int left = k < n && s[k] != ' ' && s[k] != '\t';
			if (c == '_' && i > 0 && isalnum((unsigned char)s[i - 1])) left = 0;   /* snake_case */
			if (left && run <= 3 && nc < 16) {
				for (int j = k; j < n; j++) {
					if (s[j] == '`') {                  /* do not close inside a code span */
						int e = j;
						while (e < n && s[e] == '`') e++;
						int r2 = e - j, m = e;
						while (m < n) {
							if (s[m] == '`') { int q = m; while (q < n && s[q] == '`') q++; if (q - m == r2) { m = q; break; } m = q; }
							else m++;
						}
						j = m - 1;
						continue;
					}
					if (s[j] != (char)c) continue;
					int e = j;
					while (e < n && s[e] == (char)c) e++;
					int right = s[j - 1] != ' ' && s[j - 1] != '\t';
					if (c == '_' && e < n && isalnum((unsigned char)s[e])) right = 0;
					if (e - j == run && right) {
						int cls = c == '~' ? HL_STRIKE : run == 1 ? HL_EMPH : run == 2 ? HL_STRONG : HL_STRONGEMPH;
						if (cls == HL_STRONG && base == HL_EMPH) cls = HL_STRONGEMPH;
						hkMark(hl, i, e, cls);
						closers[nc++] = j;
						i = k;
						goto next;
					}
					j = e - 1;
				}
			}
			i = k;
			continue;
		}
		i++;
	next:;
	}
	return 0;
}

static int hkHlMarkdown(const char *s, int n, unsigned char *hl, int st,
                        const char *prev, int prevn, const char *next, int nextn) {
	char ch;
	if (st & MD_FM) {                                   /* --- yaml front matter --- */
		if (n >= 3 && (!strncmp(s, "---", 3) || !strncmp(s, "...", 3)) && hkBlank(s + 3, n - 3)) {
			hkMark(hl, 0, n, HL_DELIM);
			return 0;
		}
		hkHlLine(hkSyntaxByName("yaml"), s, n, hl, 0, NULL, 0, NULL, 0);
		return MD_FM;
	}
	if (st & MD_FENCE) {
		int len = hkMdFence(s, n, &ch);
		int i = hkIndent(s, n);
		if (len >= MD_FLEN(st) && (ch == '~') == !!(st & MD_TILDE) && hkBlank(s + i + len, n - i - len)) {
			hkMark(hl, 0, n, HL_DELIM);
			return 0;
		}
		int lang = MD_LANG(st);
		if (lang > 0 && lang <= HLDB_ENTRIES && HLDB[lang - 1].mode != HLM_MARKDOWN) {
			int inner = hkHlLine(&HLDB[lang - 1], s, n, hl, st & 0xFFFF, NULL, 0, NULL, 0);
			return (st & ~0xFFFF) | (inner & 0xFFFF);
		}
		hkMark(hl, 0, n, HL_CODE);
		return st;
	}
	if (!prev && n >= 3 && !strncmp(s, "---", 3) && hkBlank(s + 3, n - 3)) {   /* first line */
		hkMark(hl, 0, n, HL_DELIM);
		return MD_FM;
	}
	if (st & 1) {                                        /* inside <!-- --> */
		int e = -1;
		for (int j = 0; j + 3 <= n; j++) if (!strncmp(s + j, "-->", 3)) { e = j + 3; break; }
		if (e < 0) { hkMark(hl, 0, n, HL_COMMENT); return 1; }
		hkMark(hl, 0, e, HL_COMMENT);
		return hkMdInline(s, e, n, hl, 0);
	}
	if (hkBlank(s, n)) return 0;
	int flen = hkMdFence(s, n, &ch);
	if (flen) {
		int i = hkIndent(s, n) + flen;
		hkMark(hl, 0, n, HL_DELIM);
		while (i < n && s[i] == ' ') i++;
		int w = i;
		char name[24];
		while (i < n && s[i] != ' ' && s[i] != '{' && s[i] != ',' && i - w < 23) i++;
		memcpy(name, s + w, i - w);
		name[i - w] = '\0';
		hkMark(hl, w, i, HL_KEYWORD2);
		const struct editorSyntax *ls = name[0] ? hkSyntaxByName(name) : NULL;
		int lang = ls ? (int)(ls - HLDB) + 1 : 0;
		return MD_FENCE | (ch == '~' ? MD_TILDE : 0) | ((flen > 15 ? 15 : flen) << 18) | (lang << 22);
	}
	int i = hkIndent(s, n);
	if (i <= 3 && s[i] == '#') {                         /* # heading */
		int j = i;
		while (j < n && s[j] == '#') j++;
		if (j - i <= 6 && (j >= n || s[j] == ' ' || s[j] == '\t')) {
			hkMark(hl, 0, n, HL_HEADING);
			return 0;
		}
	}
	if (hkMdUnderline(s, n) && hkMdParagraph(prev, prevn) && !(st & MD_TABLE)) {
		hkMark(hl, 0, n, HL_HEADING);                    /* the === under a heading */
		return 0;
	}
	if (hkMdRule(s, n)) {
		hkMark(hl, 0, n, HL_DELIM);
		return 0;
	}
	/* tables: the delimiter row, the rows after it, and the header above it */
	int has_pipe = memchr(s, '|', n) != NULL;
	if (has_pipe && hkMdTableDelim(s, n)) {
		hkMark(hl, 0, n, HL_DELIM);
		return MD_TABLE;
	}
	int header = has_pipe && next && hkMdTableDelim(next, nextn);
	if (has_pipe && ((st & MD_TABLE) || header)) {
		hkMdInline(s, 0, n, hl, header ? HL_STRONG : 0);
		for (int j = 0; j < n; j++) {
			if (s[j] == '|' && hl[j] != HL_CODE) hl[j] = HL_DELIM;
			else if (header && hl[j] == HL_NORMAL) hl[j] = HL_STRONG;
		}
		return header ? 0 : MD_TABLE;
	}
	if (next && hkMdUnderline(next, nextn) && hkMdParagraph(s, n) && !(nextn > 0 && next[hkIndent(next, nextn)] == '-' && hkMdListMarker(next, nextn, hkIndent(next, nextn)))) {
		hkMark(hl, 0, n, HL_HEADING);                    /* setext heading text */
		return 0;
	}
	/* > quotes, then list markers and task boxes */
	while (i < n && s[i] == '>') {
		hl[i++] = HL_QUOTE;
		while (i < n && s[i] == ' ') i++;
	}
	int m = hkMdListMarker(s, n, i);
	if (m) {
		hkMark(hl, i, i + m, HL_MARKER);
		i += m;
		while (i < n && s[i] == ' ') i++;
		if (i + 2 < n && s[i] == '[' && (s[i + 1] == ' ' || s[i + 1] == 'x' || s[i + 1] == 'X') && s[i + 2] == ']') {
			int done = s[i + 1] != ' ';
			hkMark(hl, i, i + 3, HL_MARKER);
			if (done) { hkMark(hl, i + 3, n, HL_DONE); return 0; }
			i += 3;
		}
	}
	return hkMdInline(s, i, n, hl, 0);
}

/* One line, by the syntax's own highlighter. prev/next: the lines around it,
   for what only the neighbours decide (a setext heading, a table header). */
static int hkHlLine(const struct editorSyntax *syn, const char *s, int n, unsigned char *hl, int st,
                    const char *prev, int prevn, const char *next, int nextn) {
	memset(hl, HL_NORMAL, n);
	if (!syn) return 0;
	switch (syn->mode) {
	case HLM_MARKDOWN: return hkHlMarkdown(s, n, hl, st, prev, prevn, next, nextn);
	case HLM_MARKUP:   return hkHlMarkup(s, n, hl, st);
	case HLM_DIFF:     return hkHlDiff(s, n, hl);
	default: break;
	}
	int out = hkHlCode(syn, s, n, hl, st);
	if (syn->mode == HLM_INI && ST_KIND(st) == ST_NONE) hkHlIni(s, n, hl);
	else if (syn->mode == HLM_YAML) hkHlYaml(s, n, hl);
	else if (syn->mode == HLM_MAKE) hkHlMake(s, n, hl);
	return out;
}

static const struct editorSyntax *hkSyntaxByName(const char *name) {
	static const char *const alias[][2] = {
		{"h", "c"}, {"cpp", "c++"}, {"cxx", "c++"}, {"cc", "c++"}, {"py", "python"}, {"python3", "python"},
		{"js", "javascript"}, {"jsx", "javascript"}, {"node", "javascript"}, {"ts", "typescript"},
		{"tsx", "typescript"}, {"sh", "shell"}, {"bash", "shell"}, {"zsh", "shell"}, {"console", "shell"},
		{"shell-session", "shell"}, {"yml", "yaml"}, {"golang", "go"}, {"rs", "rust"}, {"rb", "ruby"},
		{"kt", "kotlin"}, {"cs", "c#"}, {"csharp", "c#"}, {"make", "makefile"}, {"mk", "makefile"},
		{"patch", "diff"}, {"svg", "xml"}, {"dockerfile", "docker"}, {"conf", "ini"}, {"cfg", "ini"},
		{"asm", "assembly"}, {"md", "markdown"}, {"ex", "elixir"}, {"hs", "haskell"}, {"clj", "clojure"},
		{"jl", "julia"}, {"ml", "ocaml"}, {"fs", "f#"}, {"fsharp", "f#"}, {"pl", "perl"}, {"hakorc", "hakorc"},
	};
	char low[32];
	int k = 0;
	for (; name[k] && k < 31; k++) low[k] = (char)tolower((unsigned char)name[k]);
	low[k] = '\0';
	for (int a = 0; a < (int)(sizeof(alias) / sizeof(alias[0])); a++)
		if (!strcmp(low, alias[a][0])) { strcpy(low, alias[a][1]); break; }
	for (int j = 0; j < HLDB_ENTRIES; j++)
		if (!strcmp(HLDB[j].filetype, low)) return &HLDB[j];
	return NULL;
}

/* by extension, or by the whole file name (Makefile, Dockerfile.dev, .bashrc) */
static const struct editorSyntax *hkSyntaxForFile(const char *path) {
	if (!path) return HL_TEXT;
	const char *base = path;
	for (const char *q = path; *q; q++) if (*q == '/') base = q + 1;
	const char *ext = strrchr(base, '.');
	for (int j = 0; j < HLDB_ENTRIES - 1; j++) {
		for (int i = 0; HLDB[j].filematch[i]; i++) {
			const char *m = HLDB[j].filematch[i];
			size_t ml = strlen(m);
			if (m[0] == '.' && ext && !strcasecmp(ext, m)) return &HLDB[j];
			if (!strcmp(base, m)) return &HLDB[j];
			if (m[0] != '.' && !strncmp(base, m, ml) && base[ml] == '.') return &HLDB[j];
		}
	}
	return HL_TEXT;
}

/* #!/usr/bin/env python3 and friends */
static const struct editorSyntax *hkSyntaxForShebang(const char *line) {
	if (!line || line[0] != '#' || line[1] != '!') return NULL;
	static const char *const map[][2] = {
		{"python", "python"}, {"bash", "shell"}, {"zsh", "shell"}, {"/sh", "shell"}, {" sh", "shell"},
		{"node", "javascript"}, {"perl", "perl"}, {"ruby", "ruby"}, {"lua", "lua"}, {"make", "makefile"},
	};
	for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (strstr(line, map[i][0])) return hkSyntaxByName(map[i][1]);
	return NULL;
}


/*** terminal ***/
#ifdef _WIN32
static HANDLE hStdin, hStdout;
static DWORD w32_orig_in, w32_orig_out;

void disableRawMode() {
	SetConsoleMode(hStdin, w32_orig_in);
	SetConsoleMode(hStdout, w32_orig_out);
	if (write(STDOUT_FILENO, "\x1b[?2004l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1049l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?25h", 6) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[0 q", 5) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[0m", 4) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[39m\x1b[49m", 10) < 0) {}
}

void enableRawMode() {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleMode(hStdin, &w32_orig_in);
	GetConsoleMode(hStdout, &w32_orig_out);
	atexit(disableRawMode);

	DWORD in_mode = w32_orig_in;
	in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
	in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (E.mouse_enabled) in_mode |= ENABLE_MOUSE_INPUT;
	SetConsoleMode(hStdin, in_mode);

	DWORD out_mode = w32_orig_out;
	out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
	SetConsoleMode(hStdout, out_mode);

	if (write(STDOUT_FILENO, "\x1b[?1049h", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?2004h", 8) < 0) {}
	if (E.mouse_enabled) {
		if (write(STDOUT_FILENO, "\x1b[?1000h", 8) < 0) {}
		if (write(STDOUT_FILENO, "\x1b[?1002h", 8) < 0) {}
		if (write(STDOUT_FILENO, "\x1b[?1006h", 8) < 0) {}
	}
}

int getWindowSize(int *rows, int *cols) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(hStdout, &csbi)) return -1;
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 0;
}

int editorInputPending() {
	DWORD events = 0;
	GetNumberOfConsoleInputEvents(hStdin, &events);
	return events > 1;
}

#endif

void die(const char *s) {
	if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) {}
	perror(s);
	exit(1);
}

void detectTerminalType() {
	char *term = getenv("TERM");
	char *colorterm = getenv("COLORTERM");
	char *term_program = getenv("TERM_PROGRAM");

	E.term_type = TERM_BASIC;

	if (term_program && strcmp(term_program, "Apple_Terminal") == 0) {
		E.term_type = TERM_XTERM_256;
		return;
	}

	if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
		E.term_type = TERM_TRUECOLOR;
		return;
	}

	if (term && (strstr(term, "256color") || strstr(term, "kitty") ||
		strstr(term, "alacritty") || strstr(term, "ghostty") ||
		strstr(term, "wezterm") || strstr(term, "iterm") ||
		strstr(term, "xterm"))) {
		E.term_type = TERM_XTERM_256;
	}
}

#ifndef _WIN32
volatile sig_atomic_t winch_received = 0;

static void on_sigwinch(int signo) {
	(void)signo;
	winch_received = 1;
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
	
	if (write(STDOUT_FILENO, "\x1b[?2004l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1000l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1002l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1003l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1015l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1006l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?1049l", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?25h", 6) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[0 q", 5) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[0m", 4) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[39m\x1b[49m", 10) < 0) {}
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= CS8;
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

	if (write(STDOUT_FILENO, "\x1b[?1049h", 8) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) {}
	if (write(STDOUT_FILENO, "\x1b[?2004h", 8) < 0) {}
	if (E.mouse_enabled) {
		if (write(STDOUT_FILENO, "\x1b[?1000h", 8) < 0) {}
		if (write(STDOUT_FILENO, "\x1b[?1002h", 8) < 0) {}
		if (write(STDOUT_FILENO, "\x1b[?1006h", 8) < 0) {}
	}
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return -1;
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

int editorInputPending() {
	fd_set fds;
	struct timeval tv = {0, 0};
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

#endif

/*** buffer ***/
void abAppend(struct abuf *ab, const char *s, int len) {
	int needed = ab->len + len;
	if (needed > ab->cap) {
		int newcap = ab->cap < 1024 ? 1024 : ab->cap;
		while (newcap < needed) newcap *= 2;
		char *new = realloc(ab->b, newcap);
		if (new == NULL) return;
		ab->b = new;
		ab->cap = newcap;
	}
	memcpy(&ab->b[ab->len], s, len);
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** cell grid (diff-render) ***/
static int gridColorEq(Color a, Color b) {
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void gridAttrs(struct abuf *out, int at) {
	char buf[24];
	int n = snprintf(buf, sizeof(buf), "\x1b[0%s%s%s%s%s%sm",
		(at & HK_A_BOLD) ? ";1" : "", (at & HK_A_DIM) ? ";2" : "",
		(at & HK_A_ITALIC) ? ";3" : "", (at & HK_A_UNDER) ? ";4" : "",
		(at & HK_A_REV) ? ";7" : "", (at & HK_A_STRIKE) ? ";9" : "");
	abAppend(out, buf, n);
}

void gridResize(int w, int h) {
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w > 4096) w = 4096;
	if (h > 4096) h = 4096;
	if (w == E.grid_w && h == E.grid_h && E.grid_front && E.grid_back) return;
	free(E.grid_front);
	free(E.grid_back);
	E.grid_w = w;
	E.grid_h = h;
	size_t n = (size_t)w * (size_t)h;
	E.grid_front = calloc(n, sizeof(Cell));
	E.grid_back = calloc(n, sizeof(Cell));
	if (!E.grid_front || !E.grid_back) {
		free(E.grid_front); free(E.grid_back);
		E.grid_front = E.grid_back = NULL;
		E.grid_w = E.grid_h = 0;
	}
}

void gridFree(void) {
	free(E.grid_front);
	free(E.grid_back);
	E.grid_front = NULL;
	E.grid_back = NULL;
	E.grid_w = 0;
	E.grid_h = 0;
}

void gridInvalidateFront(void) {
	if (E.grid_front && E.grid_w > 0 && E.grid_h > 0) {
		memset(E.grid_front, 0, (size_t)E.grid_w * E.grid_h * sizeof(Cell));
	}
}

void gridClear(Color bg) {
	if (!E.grid_back) return;
	int n = E.grid_w * E.grid_h;
	for (int i = 0; i < n; i++) {
		Cell *c = &E.grid_back[i];
		memset(c->ch, 0, sizeof(c->ch));
		c->ch[0] = ' ';
		c->fg = E.theme.fg;
		c->bg = bg;
		c->width = 1;
	}
}

static void gridSetCell(int x, int y, const char *utf8, int byte_len, int width, Color fg, Color bg) {
	if (!E.grid_back) return;
	if (x < 0 || y < 0 || x >= E.grid_w || y >= E.grid_h) return;
	if (byte_len < 0) byte_len = 0;
	if (byte_len > 4) byte_len = 4;
	Cell *c = &E.grid_back[y * E.grid_w + x];
	memset(c->ch, 0, sizeof(c->ch));
	if (byte_len > 0) memcpy(c->ch, utf8, byte_len);
	else c->ch[0] = ' ';
	c->fg = fg;
	c->bg = bg;
	c->width = (signed char)width;
	if (width == 2 && x + 1 < E.grid_w) {
		Cell *cc = &E.grid_back[y * E.grid_w + x + 1];
		memset(cc->ch, 0, sizeof(cc->ch));
		cc->fg = fg;
		cc->bg = bg;
		cc->width = 0;
	}
}

int gridPutStr(int x, int y, const char *s, int max_cols, Color fg, Color bg) {
	if (!s || max_cols <= 0) return 0;
	int n = (int)strlen(s);
	int i = 0;
	int cols_used = 0;
	while (i < n && cols_used < max_cols) {
		unsigned char b = (unsigned char)s[i];
		if (b < 0x20) { i++; continue; }
		int blen = utf8_byte_length(b);
		if (i + blen > n) blen = n - i;
		int w = utf8_char_width(s, i);
		if (w < 1) w = 1;
		if (cols_used + w > max_cols) break;
		gridSetCell(x + cols_used, y, &s[i], blen, w, fg, bg);
		cols_used += w;
		i += blen;
	}
	return cols_used;
}

int gridPutCh(int x, int y, char c, Color fg, Color bg) {
	gridSetCell(x, y, &c, 1, 1, fg, bg);
	return 1;
}

void gridFlush(struct abuf *out) {
	if (!E.grid_back || !E.grid_front) return;
	Color cur_fg = { -1, -1, -1, 0 };
	Color cur_bg = { -1, -1, -1, 0 };
	int cur_x = -1, cur_y = -1, cur_at = -1;
	for (int y = 0; y < E.grid_h; y++) {
		for (int x = 0; x < E.grid_w; x++) {
			int idx = y * E.grid_w + x;
			Cell *bk = &E.grid_back[idx];
			Cell *fr = &E.grid_front[idx];
			if (bk->width == 0) continue;
			int same = (bk->width == fr->width
				&& memcmp(bk->ch, fr->ch, sizeof(bk->ch)) == 0
				&& gridColorEq(bk->fg, fr->fg)
				&& gridColorEq(bk->bg, fr->bg));
			if (same) continue;
			if (cur_y != y || cur_x != x) {
				char buf[32];
				snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
				abAppend(out, buf, strlen(buf));
				cur_x = x;
				cur_y = y;
			}
			int at = bk->fg.a | bk->bg.a;
			if (at != cur_at) {               /* a reset drops the colors too */
				gridAttrs(out, at);
				cur_at = at;
				cur_fg = cur_bg = (Color){ -1, -1, -1, 0 };
			}
			if (!gridColorEq(cur_fg, bk->fg)) {
				setThemeColor(out, bk->fg);
				cur_fg = bk->fg;
			}
			if (!gridColorEq(cur_bg, bk->bg)) {
				setThemeBgColor(out, bk->bg);
				cur_bg = bk->bg;
			}
			int blen = 0;
			while (blen < (int)sizeof(bk->ch) && bk->ch[blen]) blen++;
			if (blen == 0) { abAppend(out, " ", 1); }
			else abAppend(out, bk->ch, blen);
			cur_x += (bk->width > 0 ? bk->width : 1);
			*fr = *bk;
			if (bk->width == 2 && x + 1 < E.grid_w) {
				E.grid_front[idx + 1] = E.grid_back[idx + 1];
			}
		}
	}
}

/*** helpers ***/
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}:\"'`!@#$%^&*|\\?", c) != NULL;
}

int getCharWidth(char c, int col) {
	if (c == '\t') {
		return E.tab_stop - (col % E.tab_stop);
	}
	return 1;
}

int getDisplayWidth(const char *str, int len, int col) {
	int width = 0;
	for (int i = 0; i < len; i++) {
		width += getCharWidth(str[i], col + width);
	}
	return width;
}

int utf8_byte_length(unsigned char c) {
	if ((c & 0x80) == 0) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

int utf8_char_width(const char *s, int pos) {
	unsigned char c = s[pos];
	if ((c & 0x80) == 0) return 1;
	
	int len = utf8_byte_length(c);
	if (len == 3) {
		if (c == 0xE2) {
			unsigned char c1 = s[pos + 1];
			if ((c1 >= 0x94 && c1 <= 0x96) || (c1 >= 0x98 && c1 <= 0x9F)) {
				return 1;
			}
		}
		
		if ((c >= 0xE3 && c <= 0xE9) || c == 0xEF) {
			return 2;
		}
	}
	return 1;
}

int utf8_next_char(const char *s, int pos, int size) {
	if (pos >= size) return size;
	int len = utf8_byte_length((unsigned char)s[pos]);
	pos += len;
	if (pos > size) return size;
	return pos;
}

int utf8_prev_char(const char *s, int pos) {
	if (pos <= 0) return 0;
	pos--;
	while (pos > 0 && (s[pos] & 0xC0) == 0x80) pos--;
	return pos;
}

static int utf8_display_width(const char *s) {
	if (!s) return 0;
	int col = 0;
	int i = 0;
	int n = (int)strlen(s);
	while (i < n) {
		col += utf8_char_width(s, i);
		i = utf8_next_char(s, i, n);
	}
	return col;
}

/* A Rei history line wrapped at w cells, breaking after the last space that fits and mid-word only
   when a word is longer than the row. Fills the sub-th row and returns 0; with sub < 0 returns the
   row count. A wrapped row drops the space it broke on. */
static int hkReiWrap(const char *s, int w, int sub, int *out_off, int *out_len, int *out_cols) {
	int n = s ? (int)strlen(s) : 0, i = 0, rows = 0;
	if (w < 1) w = 1;
	do {
		int j = i, c = 0, sp = -1, sp_c = 0;
		while (j < n) {
			int cw = utf8_char_width(s, j);
			if (c + cw > w && c > 0) break;
			if (s[j] == ' ') { sp = j; sp_c = c; }
			c += cw;
			j = utf8_next_char(s, j, n);
		}
		int end = j, cols = c, next = j;
		if (j < n && sp > i) { end = sp; cols = sp_c; next = sp + 1; }
		if (rows == sub) {
			if (out_off) *out_off = i;
			if (out_len) *out_len = end - i;
			if (out_cols) *out_cols = cols;
			return 0;
		}
		rows++;
		i = next;
	} while (i < n);
	if (sub >= 0) { if (out_off) *out_off = n; if (out_len) *out_len = 0; if (out_cols) *out_cols = 0; return 0; }
	return rows;
}

static int hkWrapWidth(editorPane *pane) {
	int w = pane->width - E.line_number_width;
	return w < 1 ? 1 : w;
}

static int hkPaneCount(void) {
	return E.num_panes + (E.left_panel != NULL) + (E.right_panel != NULL) + (E.bottom_panel != NULL);
}

/* With more than one pane, editor panes get a title bar so they match the framed
   Kami/Rei side panels. One row, stolen from the top; every consumer of an editor
   leaf's geometry (draw, scroll, cursor, mouse) adds it. A lone pane has none:
   vim's look. 0 too when disabled or the pane is too short to spare a row. */
static int hkPaneHdr(const editorPane *pane) {
	if (!E.pane_titles || !pane || pane->type != PANE_EDITOR || hkPaneCount() < 2) return 0;
	return pane->height >= 4 ? 1 : 0;
}

/* A wrapped row is laid out in screen CELLS, the way it is drawn: a character
   that would cross the right edge starts the next segment. Counting bytes (or
   assuming every segment is exactly w cells) put the cursor several characters
   off on a wrapped line with katakana in it. */
static void hkCellStep(erow *row, int i, int *bytes, int *cells) {
	unsigned char c = (unsigned char)row->render[i];
	int len = c < 128 ? 1 : utf8_byte_length(c);
	if (i + len > row->rsize) { *bytes = 1; *cells = 1; return; }   /* drawn as '?' */
	int cw = c < 128 ? 1 : utf8_char_width(row->render, i);
	*bytes = len; *cells = cw < 1 ? 1 : cw;
}

/* Walks the row once. Fills, for render column rx, its segment and the cell it
   starts at within that segment; returns the number of segments. Also gives the
   render byte where segment `want` starts and the rx found at (want, col). */
static int hkWrapWalk(erow *row, int w, int rx, int *seg, int *col,
                      int want, int *want_byte, int want_col, int *want_rx) {
	int i = 0, c = 0, s = 0, sc = 0, found = 0, got_rx = 0, last_in_want = -1;
	if (want_byte) *want_byte = (want <= 0) ? 0 : row->rsize;
	while (i < row->rsize) {
		int nb, cw;
		hkCellStep(row, i, &nb, &cw);
		if (sc > 0 && sc + cw > w) {           /* does not fit: next segment */
			s++; sc = 0;
			if (s == want && want_byte) *want_byte = i;
		}
		if (!found && c >= rx) { if (seg) *seg = s; if (col) *col = sc; found = 1; }
		if (s == want) {
			last_in_want = c;
			if (!got_rx && want_col < sc + cw) { if (want_rx) *want_rx = c; got_rx = 1; }
		}
		i += nb; c += cw; sc += cw;
	}
	int segs = s + 1;
	if (!found) {                               /* the cursor after the last character */
		if (seg) *seg = (sc >= w) ? s + 1 : s;
		if (col) *col = (sc >= w) ? 0 : sc;
	}
	if (want_rx && !got_rx) {
		/* past the end of segment `want`: its last character; past the row, its end */
		*want_rx = (want >= segs - 1 || last_in_want < 0) ? c : last_in_want;
	}
	return segs;
}

static int hkRowWrapLines(erow *row, int w) {
	return hkWrapWalk(row, w, 1 << 30, NULL, NULL, -1, NULL, 0, NULL);
}

/* Segment and in-segment cell for render column rx. */
static void hkWrapPos(erow *row, int w, int rx, int *seg, int *col) {
	hkWrapWalk(row, w, rx, seg, col, -1, NULL, 0, NULL);
}

/* The render column of the character at (seg, col): the cursor target for
   up/down and for a mouse click; clamped to the end of that segment. */
static int hkWrapRxAt(erow *row, int w, int seg, int col) {
	int rx = 0;
	hkWrapWalk(row, w, 1 << 30, NULL, NULL, seg, NULL, col, &rx);
	return rx;
}

/* The render byte where segment `seg` starts. */
static int hkWrapByteAt(erow *row, int w, int seg) {
	int b = 0;
	hkWrapWalk(row, w, 1 << 30, NULL, NULL, seg, &b, 0, NULL);
	return b;
}

static int hkVisualRowOfLine(editorPane *pane, int y, int w) {
	int v = 0;
	for (int i = 0; i < y; i++) v += hkRowWrapLines(&pane->row[i], w);
	return v;
}

static int hkRowAtVisual(editorPane *pane, int target, int w, int *seg) {
	int v = 0;
	for (int r = 0; r < pane->numrows; r++) {
		int lines = hkRowWrapLines(&pane->row[r], w);
		if (v + lines > target) {
			if (seg) *seg = target - v;
			return r;
		}
		v += lines;
	}
	return -1;
}

/*** color ***/
/* Nearest xterm-256 entry: the cube's real levels, and the grey ramp for greys.
   Rounding each channel down to a sixth (r*5/255) threw away half the cube and
   turned every theme muddy in Terminal.app, which has no true colour. */
static int hkRgbTo256(Color c) {
	int r = c.r, g = c.g, b = c.b;
	static const int q[] = {0, 95, 135, 175, 215, 255};
	int ri = 0, gi = 0, bi = 0;
	for (int i = 0; i < 6; i++) {
		if (abs(r - q[i]) < abs(r - q[ri])) ri = i;
		if (abs(g - q[i]) < abs(g - q[gi])) gi = i;
		if (abs(b - q[i]) < abs(b - q[bi])) bi = i;
	}
	int cr = q[ri] - r, cg = q[gi] - g, cb = q[bi] - b;
	int v = (r + g + b) / 3, gs = v < 8 ? 0 : (v > 238 ? 23 : (v - 8) / 10);
	int gv = 8 + gs * 10, dr = gv - r, dg = gv - g, db = gv - b;
	/* the cube rounds each channel alone, so a warm paper lands on pink; a near
	   grey is often the closer colour, and that is the one to use */
	if (dr * dr + dg * dg + db * db < cr * cr + cg * cg + cb * cb) return 232 + gs;
	return 16 + 36 * ri + 6 * gi + bi;
}

/* The profile's own colors: the default fg/bg, or one of its 16. */
static int hkPaletteSeq(char *buf, size_t n, Color c, int bg) {
	if (c.r == HK_COL_DEFAULT) return snprintf(buf, n, bg ? "\x1b[49m" : "\x1b[39m");
	int i = c.g & 15;
	return snprintf(buf, n, "\x1b[%dm", (i < 8 ? (bg ? 40 : 30) : (bg ? 100 : 90)) + (i & 7));
}

void setThemeColor(struct abuf *ab, Color color) {
	char buf[32];

	if (color.r < 0) {
		abAppend(ab, buf, hkPaletteSeq(buf, sizeof(buf), color, 0));
		return;
	}
	if (E.term_type == TERM_TRUECOLOR) {
		snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", color.r, color.g, color.b);
	} else if (E.term_type == TERM_XTERM_256) {
		int color_index = hkRgbTo256(color);
		snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", color_index);
	} else {
		int ansi_color = 40;

		if (color.r < 50 && color.g < 50 && color.b < 50) ansi_color = 40;
		else if (color.r > 180 && color.g < 100 && color.b < 100) ansi_color = 41;
		else if (color.r < 100 && color.g > 180 && color.b < 100) ansi_color = 42;
		else if (color.r > 180 && color.g > 180 && color.b < 100) ansi_color = 43;
		else if (color.r < 100 && color.g < 100 && color.b > 180) ansi_color = 44;
		else if (color.r > 180 && color.g < 100 && color.b > 180) ansi_color = 45;
		else if (color.r < 100 && color.g > 180 && color.b > 180) ansi_color = 46;
		else if (color.r > 200 && color.g > 200 && color.b > 200) ansi_color = 47;
		else {
			int intensity = (color.r + color.g + color.b) / 3;
			if (intensity > 127) ansi_color = 47;
			else if (color.b > color.r && color.b > color.g) ansi_color = 44;
			else if (color.g > color.r) ansi_color = 42;
			else if (color.r > color.g) ansi_color = 41;
		}

		snprintf(buf, sizeof(buf), "\x1b[%dm", ansi_color);
	}
	abAppend(ab, buf, strlen(buf));
}

void setThemeBgColor(struct abuf *ab, Color color) {
	char buf[32];

	if (color.r < 0) {
		abAppend(ab, buf, hkPaletteSeq(buf, sizeof(buf), color, 1));
		return;
	}
	if (E.term_type == TERM_TRUECOLOR) {
		snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", color.r, color.g, color.b);
	} else if (E.term_type == TERM_XTERM_256) {
		int color_index = hkRgbTo256(color);
		snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", color_index);
	} else {
		int intensity = (color.r + color.g + color.b) / 3;
		int ansi_color = 40;
		
		if (intensity < 50) ansi_color = 40;
		else if (intensity > 200) ansi_color = 47;
		else ansi_color = 40;
		
		snprintf(buf, sizeof(buf), "\x1b[%dm", ansi_color);
	}
	abAppend(ab, buf, strlen(buf));
}

/* Code is drawn in the theme's own colours. It was a fixed VS Code palette for
   every theme, so a light theme lost its operators (light grey on paper) and
   the comment/keyword/string slots each theme defines were never used. */
Color editorSyntaxToRGB(int hl) {
	const Theme *t = &E.theme;
	Color c;
	switch (hl) {
	case HL_COMMENT: case HL_MLCOMMENT: case HL_DELIM:     return t->comment;
	case HL_KEYWORD1: case HL_TAG: case HL_MARKER:         return t->keyword1;
	case HL_KEYWORD2: case HL_TYPE: case HL_LABEL: case HL_PREPROCESSOR:
	case HL_VARIABLE: case HL_ATTRNAME: case HL_KEY: case HL_QUOTE: return t->keyword2;
	case HL_STRING: case HL_CHAR: case HL_CODE: case HL_DIFF_ADD: return t->string;
	case HL_NUMBER: case HL_CONSTANT: case HL_ESCAPE:     return t->number;
	case HL_DIFF_DEL:                                     return t->err;
	case HL_HEADING:    c = t->keyword1; c.a |= HK_A_BOLD; return c;
	case HL_STRONG:     c = t->fg; c.a |= HK_A_BOLD; return c;
	case HL_EMPH:       c = t->fg; c.a |= HK_A_ITALIC; return c;
	case HL_STRONGEMPH: c = t->fg; c.a |= HK_A_BOLD | HK_A_ITALIC; return c;
	case HL_STRIKE:     c = t->comment; c.a |= HK_A_STRIKE; return c;
	case HL_LINK:       c = t->keyword2; c.a |= HK_A_UNDER; return c;
	case HL_URL:        c = t->comment; c.a |= HK_A_UNDER; return c;
	case HL_DONE:       c = t->comment; c.a |= HK_A_STRIKE; return c;
	default:                                              return t->fg;
	}
}

/*** clipboard ***/

static int hkSystemClipboardWrite(const char *data, int len) {
	if (!data || len <= 0) return 0;
	const char *cmd = NULL;
#ifdef __APPLE__
	cmd = "pbcopy";
#elif defined(_WIN32)
	cmd = "clip";
#else
	if (getenv("WAYLAND_DISPLAY")) cmd = "wl-copy";
	else if (getenv("DISPLAY"))    cmd = "xclip -selection clipboard -i";
	else                            cmd = "xsel -bi";
#endif
	FILE *fp = popen(cmd, "w");
	if (!fp) return 0;
	size_t off = 0;
	while (off < (size_t)len) {
		size_t n = fwrite(data + off, 1, len - off, fp);
		if (n == 0) break;
		off += n;
	}
	int rc = pclose(fp);
	return (rc == 0 && off == (size_t)len);
}

static int hkSystemClipboardRead(char **out, size_t *out_len) {
	*out = NULL; *out_len = 0;
	const char *cmd = NULL;
#ifdef __APPLE__
	cmd = "pbpaste";
#elif defined(_WIN32)
	cmd = "powershell -NoProfile -Command Get-Clipboard";
#else
	if (getenv("WAYLAND_DISPLAY")) cmd = "wl-paste --no-newline";
	else if (getenv("DISPLAY"))    cmd = "xclip -selection clipboard -o";
	else                            cmd = "xsel -bo";
#endif
	FILE *fp = popen(cmd, "r");
	if (!fp) return 0;
	char buf[4096];
	char *accum = NULL;
	size_t total = 0;
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) {
		char *na = realloc(accum, total + got + 1);
		if (!na) { free(accum); pclose(fp); return 0; }
		accum = na;
		memcpy(accum + total, buf, got);
		total += got;
	}
	pclose(fp);
	if (!accum) return 0;
	accum[total] = '\0';
	*out = accum; *out_len = total;
	return 1;
}

void editorFreePasteBuffer() {
	free(E.paste.data);
	free(E.system_paste.data);
	E.paste.data = NULL;
	E.system_paste.data = NULL;
}

static int hkRegIndex(char name) {
	if (name >= 'a' && name <= 'z') return name - 'a';
	if (name >= 'A' && name <= 'Z') return name - 'A';
	if (name == '"' || name == 0) return 26;
	if (name == '0') return 27;
	if (name == '+' || name == '*') return 28;
	if (name == '-') return 29;
	return -1;
}

static void hkRegAssign(pasteBuffer *r, const char *data, int len, int is_line) {
	free(r->data);
	if (len > PASTE_BUFFER_MAX) len = PASTE_BUFFER_MAX;
	r->data = malloc(len + 1);
	if (!r->data) { r->len = 0; return; }
	memcpy(r->data, data, len);
	r->data[len] = '\0';
	r->len = len;
	r->is_line_mode = is_line;
}

static void hkRegAppend(pasteBuffer *r, const char *data, int len, int is_line) {
	int newlen = r->len + len;
	if (newlen > PASTE_BUFFER_MAX) newlen = PASTE_BUFFER_MAX;
	char *nb = malloc(newlen + 1);
	if (!nb) return;
	if (r->data && r->len > 0) memcpy(nb, r->data, r->len);
	int copy = newlen - r->len;
	if (copy > 0) memcpy(nb + r->len, data, copy);
	nb[newlen] = '\0';
	free(r->data);
	r->data = nb;
	r->len = newlen;
	r->is_line_mode = is_line;
}

void editorSetPasteBuffer(const char *data, int len, int is_line_mode) {
	if (len > PASTE_BUFFER_MAX) {
		len = PASTE_BUFFER_MAX;
		editorSetStatusMessage("Paste buffer truncated to %d bytes", PASTE_BUFFER_MAX);
	}
	hkRegAssign(&E.paste, data, len, is_line_mode);
}

void hkSetRegister(char name, const char *data, int len, int is_line, int is_yank) {
	if (name == '_') return;

	int append = (name >= 'A' && name <= 'Z');
	int idx = hkRegIndex(name);

	if (idx >= 0) {
		if (name == '+' || name == '*') {
			hkSystemClipboardWrite(data, len);
			hkRegAssign(&E.hk.registers[idx], data, len, is_line);
		} else if (append) {
			hkRegAppend(&E.hk.registers[idx], data, len, is_line);
		} else {
			hkRegAssign(&E.hk.registers[idx], data, len, is_line);
		}

		if (name != '"' && name != 0) {
			hkRegAssign(&E.hk.registers[26], data, len, is_line);
		}
	}

	editorSetPasteBuffer(data, len, is_line);

	if (is_yank) {
		hkRegAssign(&E.hk.registers[27], data, len, is_line);
	}
}

pasteBuffer *hkGetRegister(char name) {
	if (name == '+' || name == '*') {
		char *accum = NULL; size_t total = 0;
		if (hkSystemClipboardRead(&accum, &total) && accum) {
			int is_line = (total > 0 && accum[total - 1] == '\n');
			int idx = hkRegIndex(name);
			hkRegAssign(&E.hk.registers[idx], accum, total, is_line);
			free(accum);
			return &E.hk.registers[idx];
		}
	}
	int idx = hkRegIndex(name);
	if (idx < 0) return &E.paste;
	if (E.hk.registers[idx].data == NULL && (name == 0 || name == '"')) return &E.paste;
	return &E.hk.registers[idx];
}

void hkFreeRegisters(void) {
	for (int i = 0; i < 32; i++) {
		free(E.hk.registers[i].data);
		E.hk.registers[i].data = NULL;
		E.hk.registers[i].len = 0;
	}
}

void editorCopyToSystemClipboard() {
	if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
		editorYankVisualSelection();
	}
	if (!E.paste.data || E.paste.len == 0) return;
	if (hkSystemClipboardWrite(E.paste.data, E.paste.len)) {
		editorSetStatusMessage("Copied %d bytes to system clipboard", E.paste.len);
	} else {
		editorSetStatusMessage("Clipboard write failed (no pbcopy / xclip / wl-copy on PATH)");
	}
}

void editorPasteFromSystemClipboard() {
	char *accum = NULL; size_t total = 0;
	if (!hkSystemClipboardRead(&accum, &total) || !accum || total == 0) {
		editorSetStatusMessage("Clipboard empty or no read tool on PATH");
		free(accum);
		return;
	}

	free(E.system_paste.data);
	E.system_paste.data = accum;
	E.system_paste.len = total;

	editorSaveState();
	E.is_pasting = 1;

	int saved_auto_indent = E.auto_indent;
	int saved_smart_indent = E.smart_indent;
	E.auto_indent = 0;
	E.smart_indent = 0;

	for (size_t i = 0; i < total; i++) {
		unsigned char c = (unsigned char)E.system_paste.data[i];
		if (c == '\n' || c == '\r') {
			if (c == '\r' && i + 1 < total && E.system_paste.data[i + 1] == '\n') i++;
			editorInsertNewLine();
		} else if (c == '\t') {
			editorInsertChar('\t');
		} else if (c >= 32 || c >= 0x80) {
			editorInsertChar((char)c);
		}
		}
	editorSetStatusMessage("Pasted %zu bytes from system clipboard", total);

	E.auto_indent = saved_auto_indent;
	E.smart_indent = saved_smart_indent;
	E.is_pasting = 0;
}

/*** pane management ***/
editorPane *editorCreatePane(enum paneType type, int x, int y, int width, int height) {
	editorPane *pane = calloc(1, sizeof(editorPane));
	if (!pane) return NULL;

	static int pane_id_counter = 0;
	pane->id = pane_id_counter++;
	pane->type = type;
	pane->x = x;
	pane->y = y;
	pane->width = width;
	pane->height = height;
	pane->split_ratio = 0.5;
	pane->wrap_lines = E.word_wrap;

	return pane;
}

void editorFreePane(editorPane *pane) {
	if (!pane) return;

	if (pane->split_dir != SPLIT_NONE) {
		editorFreePane(pane->child1);
		editorFreePane(pane->child2);
		free(pane);
		return;
	}

	if (pane->type == PANE_EDITOR) {
		for (int i = 0; i < pane->numrows; i++) {
			editorFreeRow(&pane->row[i]);
		}
		free(pane->row);
		free(pane->filename);
		free(pane->pane_yank_buffer);

		while (pane->undo_stack) {
			undoState *temp = pane->undo_stack;
			pane->undo_stack = pane->undo_stack->next;
			editorFreeUndoState(temp);
		}

		while (pane->redo_stack) {
			undoState *temp = pane->redo_stack;
			pane->redo_stack = pane->redo_stack->next;
			editorFreeUndoState(temp);
		}
	}

	if (pane->type == PANE_EXPLORER) {
		explorerCleanup(pane);
	}

	if (pane->type == PANE_RUNNER) {
		runnerCleanup(pane);
	}

	if (pane->type == PANE_AI && pane->ai) {
		hakoShutdown();
		pthread_mutex_destroy(&pane->ai->lock);
		for (int i = 0; i < pane->ai->history_count; i++) {
			free(pane->ai->history[i]);
		}
		free(pane->ai->history);
		free(pane->ai->history_role);
		free(pane->ai->current_prompt);
		free(pane->ai->prompt_buffer);
		free(pane->ai);
	}

	free(pane);
}

editorPane *editorFindPaneById(editorPane *root, int id) {
	if (!root) return NULL;
	if (root->id == id) return root;
	
	if (root->split_dir != SPLIT_NONE) {
		editorPane *result = editorFindPaneById(root->child1, id);
		if (result) return result;
		return editorFindPaneById(root->child2, id);
	}
	
	return NULL;
}

void editorUpdatePaneBounds(editorPane *pane) {
	if (!pane || pane->split_dir == SPLIT_NONE) return;
	
	if (pane->split_dir == SPLIT_VERTICAL) {
		int split_pos = pane->x + (int)(pane->width * pane->split_ratio);
		
		pane->child1->x = pane->x;
		pane->child1->y = pane->y;
		pane->child1->width = split_pos - pane->x - 1;
		pane->child1->height = pane->height;
		
		pane->child2->x = split_pos;
		pane->child2->y = pane->y;
		pane->child2->width = pane->x + pane->width - split_pos;
		pane->child2->height = pane->height;
	} else {
		int split_pos = pane->y + (int)(pane->height * pane->split_ratio);
		
		pane->child1->x = pane->x;
		pane->child1->y = pane->y;
		pane->child1->width = pane->width;
		pane->child1->height = split_pos - pane->y - 1;
		
		pane->child2->x = pane->x;
		pane->child2->y = split_pos;
		pane->child2->width = pane->width;
		pane->child2->height = pane->y + pane->height - split_pos;
	}
	
	editorUpdatePaneBounds(pane->child1);
	editorUpdatePaneBounds(pane->child2);
}

void editorCollectLeafPanes(editorPane *root, editorPane ***panes, int *count) {
	if (!root) return;
	
	if (root->split_dir == SPLIT_NONE) {
		*panes = realloc(*panes, sizeof(editorPane*) * (*count + 1));
		(*panes)[*count] = root;
		(*count)++;
	} else {
		editorCollectLeafPanes(root->child1, panes, count);
		editorCollectLeafPanes(root->child2, panes, count);
	}
}

static int hkLeafCount(void) {
	editorPane **leaves = NULL;
	int n = 0;
	editorCollectLeafPanes(E.root_pane, &leaves, &n);
	free(leaves);
	return n;
}

static editorPane *hkFirstEditorLeaf(void) {
	editorPane **leaves = NULL;
	int n = 0;
	editorPane *target = NULL;
	editorCollectLeafPanes(E.root_pane, &leaves, &n);
	for (int i = 0; i < n; i++) {
		if (leaves[i] && leaves[i]->type == PANE_EDITOR) {
			target = leaves[i];
			break;
		}
	}
	free(leaves);
	return target;
}

static void hkResetPaneBuffer(editorPane *pane);
static const char *hkBaseName(const char *path) {
	const char *base = path ? path : "";
	for (const char *q = base; *q; q++) if (*q == '/') base = q + 1;
	return base;
}

/* mtime to the nanosecond plus size: an agent write in the same second as the
   load is still a change. */
static unsigned long long hkFileMtime(const char *path) {
	struct stat st;
	if (!path || stat(path, &st) != 0) return 0;
	unsigned long long ns = 0;
#if defined(__APPLE__)
	ns = st.st_mtimespec.tv_nsec;
#elif !defined(_WIN32)
	ns = st.st_mtim.tv_nsec;
#endif
	return ((unsigned long long)st.st_mtime * 1000000000ULL + ns) * 31 + (unsigned long long)st.st_size + 1;
}

static void hkReloadPane(editorPane *p) {
	int cx = p->cx, cy = p->cy, rowoff = p->rowoff, coloff = p->coloff;
	editorPane *was = E.active_pane;
	E.active_pane = p;
	hkResetPaneBuffer(p);
	char *fn = strdup(p->filename);
	editorOpen(fn);
	free(fn);
	E.active_pane = was;
	p->cy = cy < p->numrows ? cy : (p->numrows ? p->numrows - 1 : 0);
	p->cx = cx; p->rowoff = rowoff; p->coloff = coloff;
	if (p->cy < p->numrows && p->cx > p->row[p->cy].size) p->cx = p->row[p->cy].size;
	p->disk_warned = 0;
	/* no full redraw: the grid diff repaints the reloaded rows; a clear flashed the
	   whole screen every time Rei saved the open file */
}

/* Rei edits files through the agent while they are open here. The buffer
   used to keep showing the old text until you reopened it — and saving that
   buffer would put the old text BACK over the agent's edit. A clean buffer
   whose file changed on disk is reloaded in place (cursor kept); a dirty one
   is told once, and left alone. */
static void editorReloadIfChanged(void) {
	editorPane **eds = NULL; int cnt = 0;
	editorCollectLeafPanes(E.root_pane, &eds, &cnt);
	for (int i = 0; i < cnt; i++) {
		editorPane *p = eds[i];
		if (!p || p->type != PANE_EDITOR || !p->filename || !p->file_mtime) continue;
		unsigned long long now = hkFileMtime(p->filename);
		if (!now || now == p->file_mtime) continue;
		if (p->dirty) {
			if (!p->disk_warned) {
				p->disk_warned = 1;
				editorSetStatusMessage("%s changed on disk (buffer modified — :e! to reload)", hkBaseName(p->filename));
			}
			continue;
		}
		hkReloadPane(p);
		editorSetStatusMessage("reloaded: %s (changed on disk)", hkBaseName(p->filename));
	}
	free(eds);
}

static void hkResetPaneBuffer(editorPane *pane) {
	for (int i = 0; i < pane->numrows; i++) editorFreeRow(&pane->row[i]);
	free(pane->row);
	pane->row = NULL;
	pane->numrows = 0;
	pane->cx = pane->cy = 0;
	pane->rowoff = pane->coloff = 0;
}

void editorSplitPane(int vertical) {
	editorPane *current = E.active_pane;
	if (!current) return;
	if (current->type != PANE_EDITOR) {
		editorSetStatusMessage("Cannot split side panel");
		return;
	}
	
	if (E.num_panes >= MAX_PANES) {
		editorSetStatusMessage("Maximum number of panes reached");
		return;
	}
	
	int min_size = vertical ? 20 : 6;
	if ((vertical && current->width < min_size * 2) || 
		(!vertical && current->height < min_size * 2)) {
		editorSetStatusMessage("Pane too small to split");
		return;
	}
	
	editorPane *new_pane = editorCreatePane(PANE_EDITOR, 0, 0, 0, 0);
	if (!new_pane) return;
	
	editorPane *container = editorCreatePane(PANE_EDITOR, current->x, current->y, 
											  current->width, current->height);
	container->split_dir = vertical ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
	container->child1 = current;
	container->child2 = new_pane;
	container->split_ratio = 0.5;
	
	if (current->parent) {
		if (current->parent->child1 == current) {
			current->parent->child1 = container;
		} else {
			current->parent->child2 = container;
		}
		container->parent = current->parent;
	} else {
		E.root_pane = container;
	}
	
	current->parent = container;
	new_pane->parent = container;
	
	editorUpdatePaneBounds(container);
	
	if (current->filename) {
		new_pane->filename = strdup(current->filename);
		new_pane->syntax = current->syntax;
		
		for (int i = 0; i < current->numrows; i++) {
			editorPane *temp = E.active_pane;
			E.active_pane = new_pane;
			editorInsertRow(new_pane->numrows, current->row[i].chars, current->row[i].size);
			E.active_pane = temp;
		}
		new_pane->dirty = 0;
	}
	
	E.active_pane = new_pane;
	E.num_panes++;
	
}

void editorClosePane() {
	editorPane *pane = E.active_pane;
	if (!pane) return;

	if (!E.force_window_command && pane->type == PANE_EDITOR && pane->dirty) {
		editorSetStatusMessage("Pane has unsaved changes. Use Ctrl-W ! to force close");
		return;
	}

	if (pane == E.right_panel) {
		editorToggleAI();
		E.force_window_command = 0;
		return;
	}
	if (pane == E.left_panel) {
		editorToggleExplorer();
		E.force_window_command = 0;
		return;
	}
	if (pane == E.bottom_panel) {
		hkCloseSidePanel(&E.bottom_panel, NULL);
		E.force_window_command = 0;
		return;
	}

	if (pane->type == PANE_EDITOR && hkLeafCount() <= 1) {
		if (E.right_panel || E.left_panel) {
			editorSetStatusMessage("Cannot close last workspace while side pane open");
			return;
		}
		editorSetStatusMessage("Cannot close the last pane. Use :q to quit");
		return;
	}

	if (E.num_panes <= 1) {
		editorSetStatusMessage("Cannot close the last pane. Use :q to quit");
		return;
	}

	if (!pane->parent) {
		editorSetStatusMessage("Cannot close root pane");
		return;
	}
	
	editorPane *parent = pane->parent;
	editorPane *sibling = (parent->child1 == pane) ? parent->child2 : parent->child1;
	int closed_second = (parent->child2 == pane);
	
	sibling->x = parent->x;
	sibling->y = parent->y;
	sibling->width = parent->width;
	sibling->height = parent->height;
	
	if (parent->parent) {
		if (parent->parent->child1 == parent) {
			parent->parent->child1 = sibling;
		} else {
			parent->parent->child2 = sibling;
		}
		sibling->parent = parent->parent;
	} else {
		E.root_pane = sibling;
		sibling->parent = NULL;
	}
	
	editorUpdatePaneBounds(sibling->parent ? sibling->parent : sibling);
	
	parent->child1 = NULL;
	parent->child2 = NULL;
	parent->split_dir = SPLIT_NONE;
	editorFreePane(pane);
	free(parent);
	
	/* The sibling can be a split itself: focusing it left the cursor in a container
	   (no rows, not in the window list), so nothing moved and ctrl-w w did nothing.
	   Focus the window inside it nearest to the one that closed. */
	editorPane *leaf = sibling;
	while (leaf->child1 && leaf->child2) leaf = closed_second ? leaf->child2 : leaf->child1;
	E.active_pane = leaf;                  /* the old active pane was `pane`, freed above */
	leaf->is_focused = 1;
	E.num_panes--;
	E.force_window_command = 0;
}

/* ctrl-w then 1-9 jumps to that editor window, r to Rei, k to Kami — the
   same panes the chord already cycles with w, addressed instead of stepped. */
static void editorFocusPane(editorPane *p) {
	if (!p || p == E.active_pane) return;
	if (E.active_pane) E.active_pane->is_focused = 0;
	E.active_pane = p;
	p->is_focused = 1;
	/* No full redraw (no \x1b[2J): only the two panes' active colors change,
	   and the grid diff repaints exactly those cells — a full clear flickers. */
}

static editorPane *editorFindPaneOfType(enum paneType type) {
	if (E.left_panel   && E.left_panel->type   == type) return E.left_panel;
	if (E.right_panel  && E.right_panel->type  == type) return E.right_panel;
	if (E.bottom_panel && E.bottom_panel->type == type) return E.bottom_panel;
	return NULL;
}

static void editorFocusEditorN(int n) {
	editorPane **eds = NULL; int cnt = 0;
	editorCollectLeafPanes(E.root_pane, &eds, &cnt);
	if (n >= 1 && n <= cnt) editorFocusPane(eds[n - 1]);
	else editorSetStatusMessage("no editor window %d (%d open)", n, cnt);
	free(eds);
}

static void editorCyclePane(int step) {
	editorPane **all_panes = malloc(sizeof(editorPane*) * (MAX_PANES + 3));
	int total = 0;
	
	if (E.left_panel) {
		all_panes[total++] = E.left_panel;
	}
	
	editorPane **editor_panes = NULL;
	int editor_count = 0;
	editorCollectLeafPanes(E.root_pane, &editor_panes, &editor_count);
	
	for (int i = 0; i < editor_count; i++) {
		all_panes[total++] = editor_panes[i];
	}
	free(editor_panes);
	
	if (E.right_panel) {
		all_panes[total++] = E.right_panel;
	}

	if (E.bottom_panel) {
		all_panes[total++] = E.bottom_panel;
	}

	if (total <= 1) {
		free(all_panes);
		return;
	}
	
	int current_idx = -1;
	for (int i = 0; i < total; i++) {
		if (all_panes[i] == E.active_pane) {
			current_idx = i;
			break;
		}
	}
	
	if (E.active_pane) E.active_pane->is_focused = 0;
	/* Focus not in the list (it should always be; a stale container once was):
	   start from the first window instead of doing nothing. */
	E.active_pane = all_panes[current_idx >= 0 ? (current_idx + step + total) % total : 0];
	E.active_pane->is_focused = 1;
	
	free(all_panes);
}

void editorNextPane() { editorCyclePane(1); }

void editorResizePanes() {
	if (!E.root_pane) return;

	int bottom_h = 0;
	if (E.bottom_panel) {
		bottom_h = E.screenrows / 3;
		if (bottom_h < 5) bottom_h = 5;
		if (bottom_h > 12) bottom_h = 12;
		if (bottom_h > E.screenrows - 6) bottom_h = MAX(0, E.screenrows - 6);
	}
	int rows = E.screenrows - bottom_h;

	int left_w = E.left_panel ? E.left_panel_width : 0;
	int right_w = E.right_panel ? E.right_panel_width : 0;
	int min_editor = 24;
	int stack_vertical = 0;

	if (E.right_panel && E.screencols < min_editor + left_w + right_w && rows >= 16) {
		stack_vertical = 1;
		right_w = 0;
	}

	if (E.screencols < min_editor + left_w + right_w) {
		if (right_w > 0 && E.screencols < min_editor + right_w) right_w = 0;
		if (left_w > 0 && E.screencols < min_editor + left_w + right_w) left_w = 0;
	}

	int left_border = (E.left_panel && left_w > 0) ? 1 : 0;
	int ai_h = 0;
	if (stack_vertical) {
		ai_h = rows / 2;
		if (ai_h < 6) ai_h = 6;
		if (ai_h > rows - 6) ai_h = rows - 6;
	}

	E.root_pane->x = left_w + left_border;
	E.root_pane->y = 0;
	E.root_pane->width = E.screencols - left_w - left_border - right_w;
	if (E.root_pane->width < 1) E.root_pane->width = 1;
	E.root_pane->height = rows - ai_h;

	if (E.left_panel) {
		E.left_panel->x = 0;
		E.left_panel->y = 0;
		E.left_panel->width = left_w;
		E.left_panel->height = rows - ai_h;
	}

	if (E.right_panel) {
		if (stack_vertical) {
			E.right_panel->x = 0;
			E.right_panel->y = rows - ai_h;
			E.right_panel->width = E.screencols;
			E.right_panel->height = ai_h;
		} else {
			E.right_panel->x = E.screencols - right_w;
			E.right_panel->y = 0;
			E.right_panel->width = right_w;
			E.right_panel->height = rows;
		}
	}

	if (E.bottom_panel) {
		E.bottom_panel->x = 0;
		E.bottom_panel->y = rows;
		E.bottom_panel->width = E.screencols;
		E.bottom_panel->height = bottom_h;
	}

	editorUpdatePaneBounds(E.root_pane);
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int bytes = 0;
	
	while (bytes < cx && bytes < row->size) {
		if (row->chars[bytes] == '\t') {
			rx += (E.tab_stop - 1) - (rx % E.tab_stop);
			rx++;
			bytes++;
		} else {
			int width = utf8_char_width(row->chars, bytes);
			rx += width;
			bytes = utf8_next_char(row->chars, bytes, row->size);
		}
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx = 0;
	
	while (cx < row->size) {
		if (row->chars[cx] == '\t') {
			cur_rx += (E.tab_stop - 1) - (cur_rx % E.tab_stop);
			cur_rx++;
			cx++;
		} else {
			int width = utf8_char_width(row->chars, cx);
			cur_rx += width;
			if (cur_rx > rx) return cx;
			cx = utf8_next_char(row->chars, cx, row->size);
		}
		if (cur_rx > rx) return cx;
	}
	return cx;
}

/* The row at idx, from the state the row above left. Search matches on top. */
static int hkHlRow(editorPane *pane, int idx) {
	erow *row = &pane->row[idx];
	unsigned char *nhl = realloc(row->hl, row->rsize > 0 ? row->rsize : 1);
	if (!nhl) return row->hl_open_comment;
	row->hl = nhl;
	memset(row->hl, HL_NORMAL, row->rsize);
	int in = idx > 0 ? pane->row[idx - 1].hl_open_comment : 0;
	const char *prev = idx > 0 ? pane->row[idx - 1].render : NULL;
	int prevn = idx > 0 ? pane->row[idx - 1].rsize : 0;
	const char *next = idx + 1 < pane->numrows ? pane->row[idx + 1].render : NULL;
	int nextn = next ? pane->row[idx + 1].rsize : 0;
	int out = pane->syntax ? hkHlLine(pane->syntax, row->render, row->rsize, row->hl, in, prev, prevn, next, nextn) : 0;
	if (E.search.query && E.search.query[0]) {
		int qlen = strlen(E.search.query);
		if (qlen > 0 && qlen <= row->rsize) {
			char *m = strstr(row->render, E.search.query);
			while (m) {
				int pos = m - row->render;
				for (int j = 0; j < qlen && pos + j < row->rsize; j++) row->hl[pos + j] = HL_MATCH;
				m = strstr(m + 1, E.search.query);
			}
		}
	}
	return out;
}

/* Rows below follow while the state they start from changes: a loop, so an
   opened comment at the top of a long file is no deep recursion. */
void editorUpdateSyntax(erow *row) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !pane->row || row < pane->row || row - pane->row > pane->numrows) {
		free(row->hl);
		row->hl = calloc(row->rsize > 0 ? row->rsize : 1, 1);
		return;
	}
	int idx = (int)(row - pane->row);
	/* markdown: this line can make the one above a heading or a table header */
	if (pane->syntax && pane->syntax->mode == HLM_MARKDOWN && idx > 0 && pane->row[idx - 1].hl) hkHlRow(pane, idx - 1);
	for (;;) {
		int before = pane->row[idx].hl_open_comment;
		int out = hkHlRow(pane, idx);
		pane->row[idx].hl_open_comment = out;
		if (out == before || idx + 1 >= pane->numrows || !pane->row[idx + 1].hl) break;
		idx++;
	}
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	for (int j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs * (E.tab_stop - 1) + 1);

	int idx = 0;
	int j = 0;
	while (j < row->size) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % E.tab_stop != 0) row->render[idx++] = ' ';
			j++;
		} else {
			int len = utf8_byte_length((unsigned char)row->chars[j]);
			if (j + len > row->size) len = row->size - j;
			for (int k = 0; k < len; k++) {
				row->render[idx++] = row->chars[j + k];
			}
			j += len;
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	row->indent = 0;
	int i = 0;
	while (i < row->size) {
		if (row->chars[i] == ' ') {
			row->indent++;
			i++;
		} else if (row->chars[i] == '\t') {
			row->indent += E.tab_stop;
			i++;
		} else {
			break;
		}
	}

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (at < 0 || at > pane->numrows) return;

	pane->row = realloc(pane->row, sizeof(erow) * (pane->numrows + 1));
	memmove(&pane->row[at + 1], &pane->row[at], sizeof(erow) * (pane->numrows - at));
	for (int j = at + 1; j <= pane->numrows; j++) pane->row[j].idx++;

	pane->row[at].idx = at;
	pane->row[at].size = len;
	pane->row[at].chars = malloc(len + 1);
	memcpy(pane->row[at].chars, s, len);
	pane->row[at].chars[len] = '\0';

	pane->row[at].rsize = 0;
	pane->row[at].render = NULL;
	pane->row[at].hl = NULL;
	pane->row[at].hl_open_comment = 0;
	pane->row[at].indent = 0;
	editorUpdateRow(&pane->row[at]);
	pane->numrows++;

	if (pane->numrows > 1 || len > 0) {
		pane->dirty++;
	}
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (at < 0 || at >= pane->numrows) return;
	editorFreeRow(&pane->row[at]);
	memmove(&pane->row[at], &pane->row[at + 1], sizeof(erow) * (pane->numrows - at - 1));
	for (int j = at; j < pane->numrows - 1; j++) pane->row[j].idx--;
	pane->numrows--;
	pane->dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

/* Removes the whole character at `at`: one byte of a 3-byte katakana left the
   file invalid UTF-8. */
void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	int end = utf8_next_char(row->chars, at, row->size);
	if (end <= at) end = at + 1;
	memmove(&row->chars[at], &row->chars[end], row->size - end + 1);
	row->size -= end - at;
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

/*** editor operations ***/
void editorInsertChar(int c) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy == pane->numrows) {
		editorInsertRow(pane->numrows, "", 0);
	}
	editorRowInsertChar(&pane->row[pane->cy], pane->cx, c);
	pane->cx++;
}

void editorDelChar() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy == pane->numrows) return;
	if (pane->cx == 0 && pane->cy == 0) return;
	if (pane->numrows == 0) return;

	erow *row = &pane->row[pane->cy];
	if (pane->cx > 0) {
		int prev_pos = utf8_prev_char(row->chars, pane->cx);
		int char_len = pane->cx - prev_pos;
		
		memmove(&row->chars[prev_pos], &row->chars[pane->cx], row->size - pane->cx + 1);
		row->size -= char_len;
		pane->cx = prev_pos;
		editorUpdateRow(row);
		pane->dirty++;
	} else {
		pane->cx = pane->row[pane->cy - 1].size;
		editorRowAppendString(&pane->row[pane->cy - 1], row->chars, row->size);
		editorDelRow(pane->cy);
		pane->cy--;
	}
}

int editorGetIndent(erow *row) {
	return row ? row->indent : 0;
}

void editorInsertNewLine() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	while (pane->cy >= pane->numrows) {
		editorInsertRow(pane->numrows, "", 0);
	}

	int indent = 0;
	if (E.auto_indent && E.mode == MODE_INSERT && pane->cy < pane->numrows && E.is_pasting == 0) {
		erow *row = &pane->row[pane->cy];
		
		if (pane->cx > 0 || (pane->cx == 0 && row->size > 0 && isspace(row->chars[0]))) {
			indent = editorGetIndent(row);
			
			/* prose (markdown, plain text) ends lines in ':' and '(' all the time: keep its indent, add none */
			int prose = !pane->syntax || pane->syntax->mode == HLM_MARKDOWN || !strcmp(pane->syntax->filetype, "text");
			if (E.smart_indent && !prose && row->size > 0 && E.is_pasting == 0) {
				int i = row->size - 1;
				while (i >= 0 && isspace(row->chars[i])) i--;
				
				if (i >= 0) {
					char end_char = row->chars[i];
					if (strchr("{:[(", end_char)) {
						indent += E.tab_stop;
					} else if (i > 0 && row->chars[i-1] == ':' && end_char != ':') {
						indent += E.tab_stop;
					}
				}
			}
		}
	}

	if (pane->cx == 0) {
		editorInsertRow(pane->cy, "", 0);
	} else {
		erow *row = &pane->row[pane->cy];
		editorInsertRow(pane->cy + 1, &row->chars[pane->cx], row->size - pane->cx);
		row = &pane->row[pane->cy];
		row->size = pane->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}

	pane->cy++;
	pane->cx = 0;

	if (indent > 0 && E.is_pasting == 0) {
		if (E.use_tabs) {
			int num_tabs = indent / E.tab_stop;
			int num_spaces = indent % E.tab_stop;
			for (int i = 0; i < num_tabs; i++) {
				editorInsertChar('\t');
			}
			for (int i = 0; i < num_spaces; i++) {
				editorInsertChar(' ');
			}
		} else {
			for (int i = 0; i < indent; i++) {
				editorInsertChar(' ');
			}
		}
	}
}

void editorHandlePaste(const char *text, int len) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !text || len <= 0) return;
	
	editorSaveState();
	E.is_pasting = 1;
	
	for (int i = 0; i < len; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') {
				i++;
			}
			editorInsertNewLine();
		} else if (text[i] == '\t') {
			editorInsertChar('\t');
		} else if (text[i] >= 32 || text[i] < 0) {
			editorInsertChar(text[i]);
		}
	}
	
	E.is_pasting = 0;
	editorSetStatusMessage("Pasted %d bytes", len);
}

/*** cursor movement ***/
void editorMoveCursor(int key) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	erow *row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];

	switch (key) {
	case ARROW_LEFT:
	case 'h':
		if (pane->cx != 0 && row) {
			pane->cx = utf8_prev_char(row->chars, pane->cx);
		} else if (pane->cy > 0) {
			pane->cy--;
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
		}
		break;

	case ARROW_RIGHT:
	case 'l':
		if (row && pane->cx < row->size) {
			pane->cx = utf8_next_char(row->chars, pane->cx, row->size);
		} else if (row && pane->cx == row->size && pane->cy < pane->numrows - 1) {
			pane->cy++;
			pane->cx = 0;
		}
		break;

	case ARROW_UP:
	case 'k':
		if (E.word_wrap && pane->wrap_lines && row) {
			int wrap_width = hkWrapWidth(pane);
			
			int rx = editorRowCxToRx(row, pane->cx);
			int current_wrap_line, col;
			hkWrapPos(row, wrap_width, rx, &current_wrap_line, &col);
			
			if (current_wrap_line > 0) {
				pane->cx = editorRowRxToCx(row, hkWrapRxAt(row, wrap_width, current_wrap_line - 1, col));
				break;
			}
		}
		
		if (pane->cy > 0) {
			pane->cy--;
			if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
				int wrap_width = hkWrapWidth(pane);
				
				erow *prev_row = &pane->row[pane->cy];
				int prev_lines = hkRowWrapLines(prev_row, wrap_width);
				if (prev_lines > 1) {
					int sg, col = 0;
					if (row) hkWrapPos(row, wrap_width, pane->rx, &sg, &col);
					pane->cx = editorRowRxToCx(prev_row, hkWrapRxAt(prev_row, wrap_width, prev_lines - 1, col));
				}
			}
		}
		break;

	case ARROW_DOWN:
	case 'j':
		if (E.word_wrap && pane->wrap_lines && row) {
			int wrap_width = hkWrapWidth(pane);
			
			int rx = editorRowCxToRx(row, pane->cx);
			int current_wrap_line, col;
			hkWrapPos(row, wrap_width, rx, &current_wrap_line, &col);
			int total_wrap_lines = hkRowWrapLines(row, wrap_width);
			
			if (current_wrap_line < total_wrap_lines - 1) {
				pane->cx = editorRowRxToCx(row, hkWrapRxAt(row, wrap_width, current_wrap_line + 1, col));
				break;
			}
		}
		
		if (pane->numrows > 0 && pane->cy < pane->numrows - 1) {
			pane->cy++;
			if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
				int wrap_width = hkWrapWidth(pane);
				
				int sg, col = 0;
				if (row) hkWrapPos(row, wrap_width, pane->rx, &sg, &col);
				erow *next_row = &pane->row[pane->cy];
				pane->cx = editorRowRxToCx(next_row, hkWrapRxAt(next_row, wrap_width, 0, col));
			}
		}
		break;
	}

	row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];
	int rowlen = row ? row->size : 0;
	if (pane->cx > rowlen) {
		pane->cx = rowlen;
	}
}

/*** scrolling ***/
void editorScroll() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy >= pane->numrows) {
		pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	}

	pane->rx = 0;
	if (pane->cy < pane->numrows) {
		pane->rx = editorRowCxToRx(&pane->row[pane->cy], pane->cx);
	}

	if (E.word_wrap && pane->wrap_lines) {
		int wrap_width = hkWrapWidth(pane);
		
		int visual_row = hkVisualRowOfLine(pane, pane->cy, wrap_width);
		if (pane->cy < pane->numrows) {
			int sg, col;
			hkWrapPos(&pane->row[pane->cy], wrap_width, pane->rx, &sg, &col);
			visual_row += sg;
		}
		
		if (visual_row < pane->rowoff) {
			pane->rowoff = visual_row;
		}
		if (visual_row >= pane->rowoff + (pane->height - hkPaneHdr(pane))) {
			pane->rowoff = visual_row - (pane->height - hkPaneHdr(pane)) + 1;
		}
		
		pane->coloff = 0;
	} else {
		if (pane->cy < pane->rowoff) {
			pane->rowoff = pane->cy;
		}
		if (pane->cy >= pane->rowoff + (pane->height - hkPaneHdr(pane))) {
			pane->rowoff = pane->cy - (pane->height - hkPaneHdr(pane)) + 1;
		}

		if (pane->rx < pane->coloff) {
			pane->coloff = pane->rx;
		}
		if (pane->rx >= pane->coloff + pane->width - E.line_number_width) {
			pane->coloff = pane->rx - pane->width + E.line_number_width + 1;
		}
	}
}

/*** visual mode ***/
int editorIsInVisualSelection(int row, int col) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return 0;

	if (E.mode != MODE_VISUAL && E.mode != MODE_VISUAL_LINE) return 0;

	if (E.mode == MODE_VISUAL_LINE) {
		int start_y = pane->visual_anchor_y;
		int end_y = pane->cy;

		if (start_y > end_y) {
			int tmp = start_y;
			start_y = end_y;
			end_y = tmp;
		}

		return (row >= start_y && row <= end_y);
	}

	int start_y = pane->visual_anchor_y;
	int start_x = pane->visual_anchor_x;
	int end_y = pane->cy;
	int end_x = pane->cx;

	if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
		int tmp;
		tmp = start_y; start_y = end_y; end_y = tmp;
		tmp = start_x; start_x = end_x; end_x = tmp;
	}

	if (row >= 0 && row < pane->numrows) {
		int char_col = 0;
		int render_col = 0;
		erow *r = &pane->row[row];

		for (char_col = 0; char_col < r->size && render_col < col; char_col++) {
			if (r->chars[char_col] == '\t') {
				render_col += (E.tab_stop - 1) - (render_col % E.tab_stop);
			}
			render_col++;
		}
		col = char_col;
	}

	if (row < start_y || row > end_y) return 0;
	if (row == start_y && col < start_x) return 0;
	if (row == end_y && col > end_x) return 0;

	return 1;
}

void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (E.mode == MODE_VISUAL_LINE) {
		*start_x = 0;
		*start_y = pane->visual_anchor_y;
		*end_x = (pane->cy < pane->numrows) ? pane->row[pane->cy].size - 1 : 0;
		if (*end_x < 0) *end_x = 0;
		*end_y = pane->cy;
	} else {
		*start_x = pane->visual_anchor_x;
		*start_y = pane->visual_anchor_y;
		*end_x = pane->cx;
		*end_y = pane->cy;
	}

	if (*start_y > *end_y || (*start_y == *end_y && *start_x > *end_x)) {
		int tmp;
		tmp = *start_x; *start_x = *end_x; *end_x = tmp;
		tmp = *start_y; *start_y = *end_y; *end_y = tmp;
	}

	if (*end_y < pane->numrows && *end_x >= pane->row[*end_y].size) {
		*end_x = pane->row[*end_y].size - 1;
		if (*end_x < 0) *end_x = 0;
	}
}

void editorYankVisualSelection() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);

	int total_len = 0;
	int is_line_mode = (E.mode == MODE_VISUAL_LINE);
	
	for (int y = start_y; y <= end_y; y++) {
		if (y >= pane->numrows) break;

		int row_start = (y == start_y && !is_line_mode) ? start_x : 0;
		int row_end = (y == end_y && !is_line_mode) ? end_x : pane->row[y].size - 1;

		if (row_start <= row_end) {
			total_len += row_end - row_start + 1;
			if (y < end_y || is_line_mode) total_len++;
		}
	}

	if (total_len == 0) return;

	char *buffer = malloc(total_len + 1);
	char *p = buffer;
	
	for (int y = start_y; y <= end_y; y++) {
		if (y >= pane->numrows) break;

		int row_start = (y == start_y && !is_line_mode) ? start_x : 0;
		int row_end = (y == end_y && !is_line_mode) ? end_x : pane->row[y].size - 1;

		if (row_start <= row_end && row_start < pane->row[y].size) {
			int len = row_end - row_start + 1;
			if (row_end >= pane->row[y].size) len = pane->row[y].size - row_start;

			memcpy(p, &pane->row[y].chars[row_start], len);
			p += len;

			if (y < end_y || is_line_mode) {
				*p++ = '\n';
			}
		}
	}
	*p = '\0';

	hkSetRegister(E.hk.pending_reg, buffer, p - buffer, is_line_mode, 1);
	E.hk.pending_reg = 0;
	free(buffer);

	int lines = end_y - start_y + 1;
	if (lines > 1) {
		editorSetStatusMessage("%d lines yanked", lines);
	} else {
		editorSetStatusMessage("Yanked %d characters", total_len);
	}
}

void editorDeleteVisualSelection() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);

	editorYankVisualSelection();

	if (start_y == end_y) {
		erow *r = &pane->row[start_y];
		if (start_x < r->size) {             /* one range, to the end of the last character */
			int stop = end_x < r->size ? utf8_next_char(r->chars, end_x, r->size) : r->size;
			memmove(&r->chars[start_x], &r->chars[stop], r->size - stop + 1);
			r->size -= stop - start_x;
			editorUpdateRow(r);
			pane->dirty++;
		}
	} else {
		if (start_x < pane->row[start_y].size) {
			pane->row[start_y].size = start_x;
			pane->row[start_y].chars[start_x] = '\0';

			if (end_x + 1 < pane->row[end_y].size) {
				editorRowAppendString(&pane->row[start_y],
					&pane->row[end_y].chars[end_x + 1],
					pane->row[end_y].size - end_x - 1);
			}
			editorUpdateRow(&pane->row[start_y]);
		}

		for (int i = 0; i < end_y - start_y; i++) {
			editorDelRow(start_y + 1);
		}
	}

	pane->cy = start_y;
	pane->cx = start_x;

	int deleted_lines = end_y - start_y + 1;
	if (E.mode == MODE_VISUAL_LINE) {
		editorSetStatusMessage("%d line%s deleted", deleted_lines, deleted_lines == 1 ? "" : "s");
	} else if (start_y == end_y) {
		int chars = end_x - start_x + 1;
		editorSetStatusMessage("%d char%s deleted", chars, chars == 1 ? "" : "s");
	} else {
		editorSetStatusMessage("%d line%s deleted", deleted_lines, deleted_lines == 1 ? "" : "s");
	}
}

/*** undo/redo ***/
undoState *editorCreateUndoState() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return NULL;

	undoState *state = calloc(1, sizeof(undoState));
	if (!state) return NULL;

	state->numrows = pane->numrows;
	state->cx = pane->cx;
	state->cy = pane->cy;
	state->filename = pane->filename ? strdup(pane->filename) : NULL;
	state->timestamp = time(NULL);

	if (E.paste.data && E.paste.len > 0) {
		state->yank_buffer = malloc(E.paste.len + 1);
		if (state->yank_buffer) {
			memcpy(state->yank_buffer, E.paste.data, E.paste.len + 1);
			state->yank_buffer_len = E.paste.len;
		}
	}

	state->next = NULL;

	if (pane->numrows > 0) {
		state->rows = calloc(pane->numrows, sizeof(erow));
		if (!state->rows) {
			free(state->filename);
			free(state->yank_buffer);
			free(state);
			return NULL;
		}

		for (int i = 0; i < pane->numrows; i++) {
			state->rows[i].size = pane->row[i].size;
			state->rows[i].chars = malloc(pane->row[i].size + 1);
			if (!state->rows[i].chars) {
				for (int j = 0; j < i; j++) {
					free(state->rows[j].chars);
				}
				free(state->rows);
				free(state->filename);
				free(state->yank_buffer);
				free(state);
				return NULL;
			}
			memcpy(state->rows[i].chars, pane->row[i].chars, pane->row[i].size + 1);

			state->rows[i].rsize = 0;
			state->rows[i].render = NULL;
			state->rows[i].hl = NULL;
			state->rows[i].hl_open_comment = 0;
			state->rows[i].indent = pane->row[i].indent;
			state->rows[i].idx = i;
		}
	} else {
		state->rows = NULL;
	}

	return state;
}

void editorFreeUndoState(undoState *state) {
	if (!state) return;

	free(state->filename);
	free(state->yank_buffer);

	if (state->rows) {
		for (int i = 0; i < state->numrows; i++) {
			free(state->rows[i].chars);
			free(state->rows[i].render);
			free(state->rows[i].hl);
		}
		free(state->rows);
	}

	free(state);
}

static void hkTrimStack(undoState **stack, int *size) {
	undoState *curr = *stack;
	undoState *prev = NULL;
	while (curr && curr->next) {
		prev = curr;
		curr = curr->next;
	}
	if (prev) {
		editorFreeUndoState(curr);
		prev->next = NULL;
		(*size)--;
	}
}

void editorSaveState() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->undo_stack && pane->undo_stack->timestamp >= time(NULL) - 1) {
		return;
	}

	if (pane->undo_stack_size >= E.max_undo_levels)
		hkTrimStack(&pane->undo_stack, &pane->undo_stack_size);

	undoState *state = editorCreateUndoState();
	if (!state) return;

	state->next = pane->undo_stack;
	pane->undo_stack = state;
	pane->undo_stack_size++;

	while (pane->redo_stack) {
		undoState *temp = pane->redo_stack;
		pane->redo_stack = pane->redo_stack->next;
		editorFreeUndoState(temp);
		pane->redo_stack_size--;
	}
}

void editorRestoreState(undoState *state) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !state) return;

	for (int i = 0; i < pane->numrows; i++) {
		editorFreeRow(&pane->row[i]);
	}
	free(pane->row);

	pane->numrows = state->numrows;
	pane->cx = state->cx;
	pane->cy = state->cy;

	if (state->yank_buffer && state->yank_buffer_len > 0) {
		editorSetPasteBuffer(state->yank_buffer, state->yank_buffer_len, 0);
	}

	if (state->numrows > 0) {
		pane->row = calloc(state->numrows, sizeof(erow));
		for (int i = 0; i < state->numrows; i++) {
			pane->row[i].size = state->rows[i].size;
			pane->row[i].chars = malloc(state->rows[i].size + 1);
			memcpy(pane->row[i].chars, state->rows[i].chars, state->rows[i].size + 1);
			pane->row[i].idx = i;
			pane->row[i].indent = state->rows[i].indent;

			pane->row[i].rsize = 0;
			pane->row[i].render = NULL;
			pane->row[i].hl = NULL;
			pane->row[i].hl_open_comment = 0;
			editorUpdateRow(&pane->row[i]);
		}
	} else {
		pane->row = NULL;
	}

	if (pane->cy >= pane->numrows) pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	if (pane->cy < 0) pane->cy = 0;

	erow *row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];
	int rowlen = row ? row->size : 0;
	if (pane->cx > rowlen) pane->cx = rowlen;

	pane->dirty++;
}

static void hkUndoRedo(int redo) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	undoState **from = redo ? &pane->redo_stack : &pane->undo_stack;
	int *from_size   = redo ? &pane->redo_stack_size : &pane->undo_stack_size;
	undoState **to   = redo ? &pane->undo_stack : &pane->redo_stack;
	int *to_size     = redo ? &pane->undo_stack_size : &pane->redo_stack_size;

	if (!*from) {
		editorSetStatusMessage(redo ? "Nothing to redo" : "Nothing to undo");
		return;
	}

	undoState *current = editorCreateUndoState();
	if (current) {
		current->next = *to;
		*to = current;
		(*to_size)++;
		if (*to_size > E.max_undo_levels) hkTrimStack(to, to_size);
	}

	undoState *state = *from;
	*from = state->next;
	(*from_size)--;

	editorRestoreState(state);
	editorFreeUndoState(state);

	editorSetStatusMessage(redo ? "Redo" : "Undo");
}

void editorUndo() {
	hkUndoRedo(0);
}

void editorRedo() {
	hkUndoRedo(1);
}

/*** search ***/
static void hkRefreshSyntax(editorPane *pane) {
	for (int i = 0; i < pane->numrows; i++)
		editorUpdateSyntax(&pane->row[i]);
}

void editorFindNext() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !E.search.query) return;

	hkRefreshSyntax(pane);

	int current_row = pane->cy;
	int current_col = pane->cx;

	for (int i = 0; i < pane->numrows; i++) {
		int row_idx = (current_row + i) % pane->numrows;
		erow *row = &pane->row[row_idx];

		char *match = strstr(row->render, E.search.query);
		if (!match) continue;

		if (row_idx == current_row && i == 0) {
			int rx = editorRowCxToRx(row, current_col);
			if (match - row->render <= rx) {
				if (rx + 1 > row->rsize) continue;
				match = strstr(row->render + rx + 1, E.search.query);
				if (!match) continue;
			}
		}

		pane->cy = row_idx;
		pane->cx = editorRowRxToCx(row, match - row->render);

		editorSetStatusMessage("/%s (n=next, N=prev)", E.search.query);
		return;
	}

	if (E.search.wrap_search) {
		editorSetStatusMessage("Search wrapped to beginning");
	} else {
		editorSetStatusMessage("Pattern not found: %s", E.search.query);
	}
}

void editorFindPrev() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !E.search.query) return;

	hkRefreshSyntax(pane);

	int current_row = pane->cy;
	int current_col = pane->cx;

	for (int i = 0; i < pane->numrows; i++) {
		int row_idx = (current_row - i + pane->numrows) % pane->numrows;
		erow *row = &pane->row[row_idx];

		char *match = strstr(row->render, E.search.query);
		char *last_match = NULL;
		int last_match_pos = -1;

		while (match) {
			int match_pos = match - row->render;

			if (row_idx == current_row && i == 0) {
				int rx = editorRowCxToRx(row, current_col);
				if (match_pos >= rx) break;
			}

			last_match = match;
			last_match_pos = match_pos;
			match = strstr(match + 1, E.search.query);
		}

		if (last_match) {
			pane->cy = row_idx;
			pane->cx = editorRowRxToCx(row, last_match_pos);

			editorSetStatusMessage("?%s (n=next, N=prev)", E.search.query);
			return;
		}
	}

	if (E.search.wrap_search) {
		editorSetStatusMessage("Search wrapped to end");
	} else {
		editorSetStatusMessage("Pattern not found: %s", E.search.query);
	}
}

void editorFind() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int saved_cx = pane->cx;
	int saved_cy = pane->cy;
	int saved_coloff = pane->coloff;
	int saved_rowoff = pane->rowoff;

	char *query = editorPrompt("Search: %s (ESC to cancel, Enter to search)", NULL);

	if (query) {
		free(E.search.query);
		E.search.query = strdup(query);
		E.search.wrap_search = 1;

		editorFindNext();

		free(query);
	} else {
		hkRefreshSyntax(pane);
		pane->cx = saved_cx;
		pane->cy = saved_cy;
		pane->coloff = saved_coloff;
		pane->rowoff = saved_rowoff;
	}
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) {
		*buflen = 0;
		return NULL;
	}

	int totlen = 0;
	for (int j = 0; j < pane->numrows; j++)
		totlen += pane->row[j].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (int j = 0; j < pane->numrows; j++) {
		memcpy(p, pane->row[j].chars, pane->row[j].size);
		p += pane->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorSelectSyntaxHighlight() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;
	const struct editorSyntax *syn = hkSyntaxForFile(pane->filename);
	if (syn == HL_TEXT && pane->numrows > 0) {
		const struct editorSyntax *sb = hkSyntaxForShebang(pane->row[0].render);
		if (sb) syn = sb;
	}
	if (pane->syntax == syn) return;
	pane->syntax = (struct editorSyntax *)syn;
	for (int i = 0; i < pane->numrows; i++) pane->row[i].hl_open_comment = 0;
	for (int i = 0; i < pane->numrows; i++) pane->row[i].hl_open_comment = hkHlRow(pane, i);
}

void editorOpen(char *filename) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	free(pane->filename);
	
	char resolved[PATH_MAX];
#ifdef _WIN32
	if (_fullpath(resolved, filename, PATH_MAX)) {
#else
	if (realpath(filename, resolved)) {
#endif
		pane->filename = strdup(resolved);
	} else {
		pane->filename = strdup(filename);
	}

	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");

	if (!fp) {
		if (errno == ENOENT) {
			pane->dirty = 0;
			editorSetStatusMessage("\"%s\" [New]", hkBaseName(pane->filename));
			return;
		} else {
			editorSetStatusMessage("Error opening file: %s", strerror(errno));
			return;
		}
	}

	unsigned char peek[4096];
	size_t peek_n = fread(peek, 1, sizeof(peek), fp);
	for (size_t i = 0; i < peek_n; i++) {
		if (peek[i] == 0) {
			fclose(fp);
			editorSetStatusMessage("Binary file refused: %s", pane->filename);
			pane->dirty = 0;
			return;
		}
	}
	rewind(fp);

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	long bytes = 0;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		bytes += linelen;
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(pane->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	if (pane->syntax == HL_TEXT) editorSelectSyntaxHighlight();     /* #! decides */
	editorSetStatusMessage("\"%s\" %dL, %ldB", hkBaseName(pane->filename), pane->numrows, bytes);
	pane->dirty = 0;
	pane->file_mtime = hkFileMtime(pane->filename);
	pane->disk_warned = 0;
}

void editorSave(int force) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	/* Rei may have written this file since it was read; saving would put the
	   old text back over it. */
	if (!force && pane->filename && pane->file_mtime) {
		unsigned long long now = hkFileMtime(pane->filename);
		if (now && now != pane->file_mtime) {
			editorSetStatusMessage("%s changed on disk since it was read — :w! to overwrite, :e! to reload",
			                       hkBaseName(pane->filename));
			return;
		}
	}

	if (pane->filename == NULL) {
		pane->filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (pane->filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int len;
	char *buf = editorRowsToString(&len);

	/* Write first, truncate after: a failed or short write must not leave the
	   file emptied. */
	int fd = open(pane->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		int off = 0;
		while (off < len) {
			ssize_t w = write(fd, buf + off, len - off);
			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) break;
			off += (int)w;
		}
		if (off == len && ftruncate(fd, len) != -1) {
#ifndef _WIN32
			fsync(fd);
#endif
			close(fd);
			free(buf);
			pane->dirty = 0;
			pane->file_mtime = hkFileMtime(pane->filename);
			pane->disk_warned = 0;
			editorSetStatusMessage("%d bytes written to disk", len);
			return;
		}
		int keep = errno;
		close(fd);
		errno = keep;
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** drawing ***/
/* The start screen with no file, the way vim does it: the ~ rows, and a few
   lines in the middle that go away on the first key. */
static void hkDrawIntro(editorPane *pane) {
	static const char *const vim[] = {"i", "to insert", ":w <file>", "to save", ":q", "to quit", ":help", "for help", NULL};
	static const char *const emacs[] = {"C-x C-f", "to open a file", "C-x C-s", "to save", "C-x C-c", "to quit", "M-x help", "for help", NULL};
	static const char *const nano[] = {"^O", "to write out", "^X", "to exit", "^G", "for help", "^T", "for a :command", NULL};
	const char *const *keys = E.keymode == HK_KEYS_EMACS ? emacs : E.keymode == HK_KEYS_NANO ? nano : vim;
	int n = 0;
	while (keys[n]) n += 2;
	n /= 2;
	int h = 3 + n;
	if (pane->height < h + 2 || pane->width < 36) return;
	int top = pane->y + (pane->height - h) / 2;
	char title[64];
	snprintf(title, sizeof(title), "hake v%s", HAKE_VERSION);
	Color strong = E.theme.fg, dim = E.theme.line_number;
	strong.a |= HK_A_BOLD;
	int tw = (int)strlen(title);
	gridPutStr(pane->x + (pane->width - tw) / 2, top, title, tw, strong, E.theme.bg);
	const char *sub = "the hako editor";
	gridPutStr(pane->x + (pane->width - (int)strlen(sub)) / 2, top + 1, sub, (int)strlen(sub), dim, E.theme.bg);
	int lw = 32, lx = pane->x + (pane->width - lw) / 2;
	for (int i = 0; i < n; i++) {
		char line[64];
		snprintf(line, sizeof(line), "type  %-10s %s", keys[i * 2], keys[i * 2 + 1]);
		gridPutStr(lx, top + 3 + i, line, pane->width - (lx - pane->x), dim, E.theme.bg);
	}
}

void editorDrawPane(editorPane *pane, struct abuf *ab) {
	(void)ab;
	if (!pane) return;

	if (pane->split_dir != SPLIT_NONE) {
		editorDrawPane(pane->child1, ab);
		editorDrawPane(pane->child2, ab);

		Color border_fg = E.theme.border;
		Color border_bg = E.theme.border;
		if (pane->split_dir == SPLIT_VERTICAL) {
			int split_x = pane->child1->x + pane->child1->width;
			for (int y = pane->y; y < pane->y + pane->height; y++) {
				gridPutStr(split_x, y, "│", 1, border_fg, border_bg);
			}
		} else {
			int split_y = pane->child1->y + pane->child1->height;
			int gx = pane->x;
			for (int x = 0; x < pane->width; x++) {
				gx += gridPutStr(gx, split_y, "─", 1, border_fg, border_bg);
			}
		}
		return;
	}

	if (pane->type == PANE_EXPLORER) {
		explorerRender(pane, ab);
		return;
	}

	if (pane->type == PANE_AI) {
		aiRender(pane, ab);
		return;
	}

	if (pane->type == PANE_RUNNER) {
		runnerRender(pane, ab);
		return;
	}

	int wrap_width = hkWrapWidth(pane);
	int hdr = hkPaneHdr(pane);
	if (hdr) {
		/* title bar: <basename>, matching the Kami/Rei panels */
		const char *base = hkBaseName(pane->filename);
		if (!*base) base = "[No Name]";
		char title[128];
		snprintf(title, sizeof(title), " %s%s", base, pane->dirty ? " \xE2\x80\xA2" : "");
		Color hf = (pane == E.active_pane) ? E.theme.status_fg : E.theme.line_number;
		Color hb = E.theme.status_bg;
		int used = gridPutStr(pane->x, pane->y, title, pane->width, hf, hb);
		for (int x = used; x < pane->width; x++) gridPutCh(pane->x + x, pane->y, ' ', hf, hb);
	}

	for (int screen_y = 0; screen_y < pane->height - hdr; screen_y++) {
		int gy = pane->y + hdr + screen_y;
		int gx = pane->x;

		int filerow;
		int wrap_line = 0;

		if (E.word_wrap && pane->wrap_lines) {
			filerow = hkRowAtVisual(pane, pane->rowoff + screen_y, wrap_width, &wrap_line);
		} else {
			filerow = pane->rowoff + screen_y;
		}

		if (filerow >= 0 && filerow < pane->numrows) {
			erow *row = &pane->row[filerow];

			if (E.show_line_numbers) {
				if (wrap_line == 0) {
					char line_num[16];
					int number = E.show_line_numbers == 2 && filerow != pane->cy ?
								abs(filerow - pane->cy) : filerow + 1;
					int w = E.line_number_width - 1;
					snprintf(line_num, sizeof(line_num), "%*d ", w, number);
					gridPutStr(gx, gy, line_num, E.line_number_width, E.theme.line_number, E.theme.bg);
				}
				gx += E.line_number_width;
			}

			/* Byte range of what this screen row shows: by cells, never by bytes
			   (a 3-byte katakana is 2 cells; mixing the two moved the cursor). */
			int start_col, end_col = row->rsize;
			if (E.word_wrap && pane->wrap_lines) {
				start_col = hkWrapByteAt(row, wrap_width, wrap_line);
			} else {
				int cells = 0, nb, cw;
				start_col = 0;
				while (start_col < row->rsize) {
					hkCellStep(row, start_col, &nb, &cw);
					if (cells >= pane->coloff) break;
					cells += cw; start_col += nb;
				}
			}

			int drawn = 0;
			for (int j = start_col; j < end_col && drawn < wrap_width; j++) {
				int is_selected = pane == E.active_pane &&
					(E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) &&
					editorIsInVisualSelection(filerow, j);

				Color cell_bg = is_selected ? E.theme.visual_bg : E.theme.bg;
				Color cell_fg;
				if (row->hl && j < row->rsize && row->hl[j] == HL_MATCH) {
					cell_fg = E.theme.visual_fg;          /* a search match: drawn like a selection */
					cell_bg = E.theme.visual_bg;
				} else if (row->hl && j < row->rsize && row->hl[j] != HL_NORMAL) {
					cell_fg = editorSyntaxToRGB(row->hl[j]);
				} else {
					cell_fg = E.theme.fg;
				}
				if (is_selected) cell_fg.a &= ~HK_A_REV;  /* the selection's own reverse is enough */

				if (j < row->rsize) {
					unsigned char c = row->render[j];
					if (c < 32 || c == 127) {
						gridPutCh(gx + drawn, gy, '?', cell_fg, cell_bg);
						drawn++;
					} else if (c < 128) {
						gridPutCh(gx + drawn, gy, (char)c, cell_fg, cell_bg);
						drawn++;
					} else {
						int char_len = utf8_byte_length(c);
						if (j + char_len <= row->rsize) {
							int w = utf8_char_width(row->render, j);
							if (w < 1) w = 1;
							if (drawn + w > wrap_width) break;
							gridPutStr(gx + drawn, gy, &row->render[j], w, cell_fg, cell_bg);
							j += char_len - 1;
							drawn += w;
						} else {
							gridPutCh(gx + drawn, gy, '?', cell_fg, cell_bg);
							drawn++;
						}
					}
				}
			}

			int fill_start = (E.show_line_numbers ? E.line_number_width : 0) + drawn;
			for (int x = fill_start; x < pane->width; x++) {
				gridPutCh(pane->x + x, gy, ' ', E.theme.fg, E.theme.bg);
			}
		} else {
			if (E.show_line_numbers) {
				for (int p = 0; p < E.line_number_width - 2; p++)
					gridPutCh(pane->x + p, gy, ' ', E.theme.line_number, E.theme.bg);
				gridPutStr(pane->x + E.line_number_width - 2, gy, "~ ", 2,
					E.theme.line_number, E.theme.bg);
			} else {
				gridPutCh(pane->x, gy, '~', E.theme.line_number, E.theme.bg);
			}
			for (int i = E.show_line_numbers ? E.line_number_width : 1; i < pane->width; i++) {
				gridPutCh(pane->x + i, gy, ' ', E.theme.fg, E.theme.bg);
			}
		}
	}
	if (E.intro_active && pane->numrows == 0 && !pane->filename && hkPaneCount() == 1) hkDrawIntro(pane);
}

/*** input ***/
int editorReadKey() {
	static int was_streaming = 0;
	static unsigned char pushed_back = 0;
	static int idle_reads = 0;
	int nread;
	char c;

	if (pushed_back) {
		c = (char)pushed_back;
		pushed_back = 0;
		return c;
	}

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");

#ifndef _WIN32
		if (winch_received) {
			winch_received = 0;
			editorUpdateWindowSize();
			editorResizePanes();
			return CTRL_KEY('l');
		}
#endif
		int is_streaming = (E.right_panel && E.right_panel->type == PANE_AI && E.right_panel->ai && E.right_panel->ai->streaming)
			|| (E.bottom_panel && E.bottom_panel->runner && E.bottom_panel->runner->running);
		if (is_streaming) { was_streaming = 1; return 0; }
		if (was_streaming) { was_streaming = 0; return 0; }
		/* ~1s idle tick so a file changed on disk shows without a keypress. */
		if (++idle_reads >= 10) { idle_reads = 0; return 0; }
	}
	idle_reads = 0;

	if (c == '\x1b') {
		char seq[32];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (seq[0] != '[' && seq[0] != 'O') {
			pushed_back = (unsigned char)seq[0];
			return '\x1b';
		}
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] == '<') {
				int idx = 2;
				while (idx < 31) {
					if (read(STDIN_FILENO, &seq[idx], 1) != 1) break;
					if (seq[idx] == 'M' || seq[idx] == 'm') {
						seq[idx + 1] = '\0';
						break;
					}
					idx++;
				}

				int button, x, y;
				char release;
				if (sscanf(&seq[2], "%d;%d;%d%c", &button, &x, &y, &release) == 4) {
					if (button == 64) {
						return MOUSE_WHEEL_UP;
					} else if (button == 65) {
						return MOUSE_WHEEL_DOWN;
					} else if ((button == 0 || button == 1 || button == 2) && release == 'M') {
						E.mouse_x = x - 1;
						E.mouse_y = y - 1;
						return MOUSE_CLICK;
					} else if (button >= 32 && button <= 35) {
						return MOUSE_MOTION;
					}
				}
				return 0;
			}
			
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1': return HOME_KEY;
					case '3': return DEL_KEY;
					case '4': return END_KEY;
					case '5': return PAGE_UP;
					case '6': return PAGE_DOWN;
					case '7': return HOME_KEY;
					case '8': return END_KEY;
					}
				}
				if (seq[1] == '1' && seq[2] == ';') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
					switch (seq[4]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
					}
					return '\x1b';
				}
				if (seq[1] == '2' && seq[2] == '0') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (seq[3] == '0') {
						if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
						if (seq[4] == '~') {
							char paste_buffer[65536];
							int paste_len = 0;
							
							editorSaveState();
							E.is_pasting = 1;
							int saved_auto_indent = E.auto_indent;
							int saved_smart_indent = E.smart_indent;
							E.auto_indent = 0;
							E.smart_indent = 0;
							
							while (1) {
								if (read(STDIN_FILENO, &c, 1) != 1) break;
								
								if (c == '\x1b') {
									char end_seq[5];
									if (read(STDIN_FILENO, &end_seq[0], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[1], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[2], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[3], 1) == 1) {
										if (end_seq[0] == '[' && end_seq[1] == '2' &&
											end_seq[2] == '0' && end_seq[3] == '1') {
											if (read(STDIN_FILENO, &c, 1) == 1 && c == '~') {
												paste_buffer[paste_len] = '\0';
												
												for (int i = 0; i < paste_len; i++) {
													if (paste_buffer[i] == '\n' || paste_buffer[i] == '\r') {
														if (paste_buffer[i] == '\r' && i + 1 < paste_len && paste_buffer[i + 1] == '\n') {
															i++;
														}
														editorInsertNewLine();
													} else if (paste_buffer[i] != '\0') {
														editorInsertChar(paste_buffer[i]);
													}
												}
												
												E.auto_indent = saved_auto_indent;
												E.smart_indent = saved_smart_indent;
												E.is_pasting = 0;
												editorSetStatusMessage("Pasted %d bytes", paste_len);
												return 0;
											}
										}
									}
								}
								
								if ((size_t)paste_len < sizeof(paste_buffer) - 1) {
									paste_buffer[paste_len++] = c;
								}
							}
							
							E.auto_indent = saved_auto_indent;
							E.smart_indent = saved_smart_indent;
							E.is_pasting = 0;
						}
					}
				}
			} else {
				switch (seq[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
			case 'H': return HOME_KEY;
			case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c == 0 ? CTRL_SPACE_KEY : c;     /* ctrl-space sends NUL: emacs's set-mark */
	}
}

/* A one-line prompt in the message line. init: the text it starts with.
   Esc, ctrl-c or ctrl-g cancel (NULL). */
static char *hkPromptWith(const char *prompt, const char *init, void (*callback)(char *, int)) {
	size_t bufsize = 128, buflen = 0;
	if (init && strlen(init) + 1 > bufsize) bufsize = strlen(init) + 64;
	char *buf = malloc(bufsize);
	if (!buf) return NULL;
	buf[0] = '\0';
	if (init) { strcpy(buf, init); buflen = strlen(init); }

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) buf[--buflen] = '\0';
		} else if (c == '\x1b' || c == CTRL_KEY('c') || c == CTRL_KEY('g')) {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		} else if (c == CTRL_KEY('u')) {
			buflen = 0;
			buf[0] = '\0';
		} else if (c >= 32 && c < 256 && c != 127) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				char *nb = realloc(buf, bufsize);
				if (!nb) { free(buf); return NULL; }
				buf = nb;
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}
		if (callback) callback(buf, c);
	}
}

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	return hkPromptWith(prompt, NULL, callback);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static editorPane *editorPaneAtCoord(int x, int y) {
	if (E.left_panel && x >= E.left_panel->x && x < E.left_panel->x + E.left_panel->width &&
		y >= E.left_panel->y && y < E.left_panel->y + E.left_panel->height) {
		return E.left_panel;
	}
	if (E.right_panel && x >= E.right_panel->x && x < E.right_panel->x + E.right_panel->width &&
		y >= E.right_panel->y && y < E.right_panel->y + E.right_panel->height) {
		return E.right_panel;
	}
	if (E.bottom_panel && x >= E.bottom_panel->x && x < E.bottom_panel->x + E.bottom_panel->width &&
		y >= E.bottom_panel->y && y < E.bottom_panel->y + E.bottom_panel->height) {
		return E.bottom_panel;
	}
	editorPane **leaves = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &leaves, &count);
	editorPane *hit = NULL;
	for (int i = 0; i < count; i++) {
		editorPane *p = leaves[i];
		if (x >= p->x && x < p->x + p->width && y >= p->y && y < p->y + p->height) {
			hit = p;
			break;
		}
	}
	free(leaves);
	return hit;
}

void editorProcessMouse(int x, int y) {
	editorPane *pane = editorPaneAtCoord(x, y);
	if (pane) {
		E.active_pane = pane;
		pane->is_focused = 1;
		if (E.left_panel && pane != E.left_panel) E.left_panel->is_focused = 0;
		if (E.right_panel && pane != E.right_panel) E.right_panel->is_focused = 0;
	}
	editorPane **panes = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &panes, &count);

	for (int i = 0; i < count; i++) {
		if (panes[i] == pane) {
			
			if (pane->type == PANE_EDITOR) {
				int rel_y = y - pane->y - hkPaneHdr(pane);
				if (rel_y < 0) rel_y = 0;
				int file_y, file_x;
				if (E.word_wrap && pane->wrap_lines) {
					int wrap_width = hkWrapWidth(pane);
					int seg = 0;
					file_y = hkRowAtVisual(pane, rel_y + pane->rowoff, wrap_width, &seg);
					if (file_y >= 0) {
						file_x = hkWrapRxAt(&pane->row[file_y], wrap_width, seg, x - pane->x - E.line_number_width);
					} else {
						file_y = pane->numrows - 1;
						file_x = 0;
					}
				} else {
					file_y = rel_y + pane->rowoff;
					file_x = x - pane->x - E.line_number_width + pane->coloff;
				}
				if (file_y >= 0 && file_y < pane->numrows && file_x >= 0) {
					pane->cy = file_y;
					pane->cx = editorRowRxToCx(&pane->row[file_y], file_x);
				}
			}
			
			for (int j = 0; j < count; j++) {
				if (j != i) panes[j]->is_focused = 0;
			}
			break;
		}
	}
	
	free(panes);
}

void editorHandleScroll(int direction) {
	editorPane *pane = editorPaneAtCoord(E.mouse_x, E.mouse_y);
	if (!pane) pane = E.active_pane;
	if (!pane) return;

	int amount = E.scroll_speed ? E.scroll_speed : 3;

	if (pane->type == PANE_AI && pane->ai) {
		if (direction == MOUSE_WHEEL_UP) pane->ai->scroll_offset += amount;
		else if (direction == MOUSE_WHEEL_DOWN) {
			pane->ai->scroll_offset -= amount;
			if (pane->ai->scroll_offset < 0) pane->ai->scroll_offset = 0;
		}
		return;
	}

	if (pane->type == PANE_RUNNER && pane->runner) {
		runnerData *r = pane->runner;
		pthread_mutex_lock(&r->lock);
		if (direction == MOUSE_WHEEL_UP) r->selected = MAX(0, r->selected - amount);
		else r->selected = MIN(r->count ? r->count - 1 : 0, r->selected + amount);
		pthread_mutex_unlock(&r->lock);
		return;
	}

	if (pane->type == PANE_EXPLORER && pane->explorer) {
		explorerData *d = pane->explorer;
		if (direction == MOUSE_WHEEL_UP) {
			d->scroll_offset -= amount;
			if (d->scroll_offset < 0) d->scroll_offset = 0;
		} else if (direction == MOUSE_WHEEL_DOWN) {
			int max_scroll = d->num_entries - (pane->height - 2);
			if (max_scroll < 0) max_scroll = 0;
			d->scroll_offset += amount;
			if (d->scroll_offset > max_scroll) d->scroll_offset = max_scroll;
		}
		return;
	}

	if (pane->type != PANE_EDITOR) return;

	if (direction == MOUSE_WHEEL_UP) {
		for (int i = 0; i < amount && pane->cy > 0; i++)
			pane->cy--;
	} else if (direction == MOUSE_WHEEL_DOWN) {
		for (int i = 0; i < amount && pane->cy < pane->numrows - 1; i++)
			pane->cy++;
	}

	if (pane->cy < pane->numrows) {
		int rowlen = pane->row[pane->cy].size;
		if (pane->cx > rowlen) pane->cx = rowlen;
	}
}

/*** mode and commands ***/
void editorSetMode(enum editorMode mode) {
	E.mode = mode;
	const char *cursor_seq = NULL;
	switch (mode) {
	case MODE_NORMAL:
		editorSetStatusMessage("-- NORMAL --");
		cursor_seq = "\x1b[2 q";
		break;
	case MODE_INSERT:
		editorSetStatusMessage("-- INSERT --");
		cursor_seq = "\x1b[6 q";
		break;
	case MODE_VISUAL:
		editorSetStatusMessage("-- VISUAL --");
		cursor_seq = "\x1b[2 q";
		break;
	case MODE_VISUAL_LINE:
		editorSetStatusMessage("-- VISUAL LINE --");
		cursor_seq = "\x1b[2 q";
		break;
	}
	if (cursor_seq && write(STDOUT_FILENO, cursor_seq, strlen(cursor_seq)) < 0) {}
}

/* :e, :config, C-x C-f, ^R: a directory opens the explorer there; a file opens
   in the editor pane, or beside it when that buffer has unsaved changes. */
static void hkOpenPath(const char *path) {
	char expanded[PATH_MAX], resolved[PATH_MAX];
	const char *filename = path;
	if (filename[0] == '~') {
		const char *home = getenv("HOME");
		if (home) {
			snprintf(expanded, sizeof(expanded), "%s%s", home, filename + 1);
			filename = expanded;
		}
	}
#ifndef _WIN32
	if (realpath(filename, resolved)) {
		filename = resolved;
	} else if (filename[0] != '/') {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd))) {
			int rn = snprintf(resolved, sizeof(resolved), "%s/%s", cwd, filename);
			if (rn > 0 && (size_t)rn < sizeof(resolved)) filename = resolved;   /* never a clipped path */
		}
	}
#else
	if (_fullpath(resolved, filename, PATH_MAX)) filename = resolved;
#endif
	struct stat est;
	if (stat(filename, &est) == 0 && S_ISDIR(est.st_mode)) {
		if (!E.left_panel) editorToggleExplorer();
		if (E.left_panel && E.left_panel->type == PANE_EXPLORER && E.left_panel->explorer) {
			explorerData *ed = E.left_panel->explorer;
			free(ed->current_dir);
			ed->current_dir = strdup(filename);
			ed->selected = 0;
			ed->scroll_offset = 0;
			explorerRefresh(E.left_panel);
			E.active_pane = E.left_panel;
			E.left_panel->is_focused = 1;
			editorSetStatusMessage("Explorer: %s", filename);
		}
		return;
	}
	editorPane *target = (E.active_pane && E.active_pane->type == PANE_EDITOR) ? E.active_pane : hkFirstEditorLeaf();
	if (!target) { editorSetStatusMessage("No editor pane"); return; }
	if (target->filename && strcmp(target->filename, filename) == 0 && !target->dirty) {
		E.active_pane = target;
		return;
	}
	if (target->dirty) {
		E.active_pane = target;
		if (explorerOpenInSplit(filename) != 1)
			editorSetStatusMessage("unsaved changes: :w first (or :e! to drop them)");
		return;
	}
	E.active_pane = target;
	hkResetPaneBuffer(target);
	editorOpen((char *)filename);
}

void editorColonCommand() {
	char *cmd = editorPrompt(":%s", NULL);
	if (cmd == NULL) return;
	editorRunColon(cmd);
}

/* Takes ownership of cmd. Also reached from Rei's normal mode, so :help, :e,
   :vsp, :theme work wherever the cursor is. */
static void editorRunColon(char *cmd) {
	editorPane *pane = E.active_pane;

	if (isdigit((unsigned char)cmd[0])) {
		if (!pane || pane->type != PANE_EDITOR) {
			editorSetStatusMessage("Not in an editor pane");
			free(cmd);
			return;
		}

		int line = atoi(cmd);
		if (line < 1) line = 1;

		if (pane->numrows == 0) {
			editorSetStatusMessage("No lines in file");
			free(cmd);
			return;
		}

		if (line > pane->numrows) {
			line = pane->numrows;
			editorSetStatusMessage("Line %d out of range, moved to line %d", atoi(cmd), line);
		}

		hkPushJump(pane->cx, pane->cy);
		pane->cy = line - 1;
		pane->cx = 0;
		pane->rowoff = pane->cy;
		free(cmd);
		return;
	}

	if (strncmp(cmd, "q ", 2) == 0 || strncmp(cmd, "quit ", 5) == 0
		|| strncmp(cmd, "close ", 6) == 0) {
		const char *target = strchr(cmd, ' ');
		while (target && *target == ' ') target++;
		if (target && *target) {
			if (strcmp(target, "rei") == 0 || strcmp(target, "ai") == 0
				|| strcmp(target, "right") == 0) {
				if (E.right_panel) editorToggleAI();
				else editorSetStatusMessage("rei not open");
			} else if (strcmp(target, "kami") == 0 || strcmp(target, "explorer") == 0
				|| strcmp(target, "left") == 0) {
				if (E.left_panel) editorToggleExplorer();
				else editorSetStatusMessage("kami not open");
			} else {
				editorSetStatusMessage("unknown pane: %s (rei|kami)", target);
			}
		}
		free(cmd);
		return;
	}

	if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
		int lcount = hkLeafCount();
		int total_panes = lcount + (E.left_panel ? 1 : 0) + (E.right_panel ? 1 : 0);

		if (E.active_pane == E.right_panel && E.right_panel) {
			editorToggleAI();
			free(cmd);
			return;
		}
		if (E.active_pane == E.left_panel && E.left_panel) {
			editorToggleExplorer();
			free(cmd);
			return;
		}
		if (E.active_pane == E.bottom_panel && E.bottom_panel) {
			hkCloseSidePanel(&E.bottom_panel, NULL);
			free(cmd);
			return;
		}

		if (total_panes > 1 && lcount > 1) {
			if (E.active_pane && E.active_pane->type == PANE_EDITOR && E.active_pane->dirty) {
				editorSetStatusMessage("Unsaved changes. :w then :q, or :q! to force.");
			} else {
				editorClosePane();
			}
			free(cmd);
			return;
		}

		int has_unsaved = 0;
		editorPane **panes = NULL;
		int count = 0;
		editorCollectLeafPanes(E.root_pane, &panes, &count);
		for (int i = 0; i < count; i++) {
			if (panes[i]->type == PANE_EDITOR && panes[i]->dirty) { has_unsaved = 1; break; }
		}
		free(panes);

		if (has_unsaved) {
			editorSetStatusMessage("Unsaved changes in panes. Use :q! to force quit.");
		} else {
			exit(0);
		}
	} else if (strcmp(cmd, "q!") == 0) {
		int lcount = hkLeafCount();
		int total_panes = lcount + (E.left_panel ? 1 : 0) + (E.right_panel ? 1 : 0);
		if (total_panes > 1 && lcount > 1) {
			E.force_window_command = 1;
			if (E.active_pane == E.right_panel && E.right_panel) editorToggleAI();
			else if (E.active_pane == E.left_panel && E.left_panel) editorToggleExplorer();
			else editorClosePane();
			free(cmd);
			return;
		}
		exit(0);
	} else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
		editorSave(0);
	} else if (strcmp(cmd, "w!") == 0) {
		editorSave(1);
	} else if (strcmp(cmd, "e!") == 0) {
		if (pane && pane->type == PANE_EDITOR && pane->filename) {
			hkReloadPane(pane);
			editorSetStatusMessage("reloaded: %s", hkBaseName(pane->filename));
		}
	} else if (strncmp(cmd, "w ", 2) == 0) {
		char *filename = cmd + 2;
		while (*filename == ' ') filename++;
		if (*filename && pane && pane->type == PANE_EDITOR) {
			free(pane->filename);
			pane->filename = strdup(filename);
			pane->file_mtime = 0;
			editorSelectSyntaxHighlight();
			editorSave(1);
		}
	} else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0 || strcmp(cmd, "wq!") == 0) {
		editorSave(cmd[2] == '!');
		if (!pane || !pane->dirty) {
			exit(0);
		}
	} else if (strcmp(cmd, "console") == 0 || strcmp(cmd, "term") == 0) {
		hkConsoleToggle();
	} else if (strcmp(cmd, "undo") == 0) {
		editorUndo();
	} else if (strcmp(cmd, "redo") == 0) {
		editorRedo();
	} else if (strcmp(cmd, "voice") == 0 || strncmp(cmd, "voice ", 6) == 0) {
		hkAgentSlash(cmd);
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
		editorPopupOpenHelp();
	} else if (strcmp(cmd, "split") == 0 || strcmp(cmd, "sp") == 0) {
		editorSplitPane(0);
	} else if (strcmp(cmd, "vsplit") == 0 || strcmp(cmd, "vs") == 0) {
		editorSplitPane(1);
	} else if (strncmp(cmd, "split ", 6) == 0 || strncmp(cmd, "sp ", 3) == 0
	           || strncmp(cmd, "vsplit ", 7) == 0 || strncmp(cmd, "vs ", 3) == 0) {
		/* vim's :sp file / :vs file: split, then open the file in the new pane */
		const char *filename = strchr(cmd, ' ');
		while (*filename == ' ') filename++;
		int before = E.num_panes;
		editorSplitPane(cmd[0] == 'v');
		if (*filename && E.num_panes > before) hkOpenPath(filename);
	} else if (strcmp(cmd, "explorer") == 0 || strcmp(cmd, "Explorer") == 0 || strcmp(cmd, "ex") == 0
	           || strcmp(cmd, "kami") == 0) {
		editorToggleExplorer();
	} else if (strcmp(cmd, "rei") == 0 || strcmp(cmd, "ai") == 0) {
		editorToggleAI();
	} else if (strcmp(cmd, "config") == 0) {
		editorGenerateConfig();
	} else if (strncmp(cmd, "config ", 7) == 0) {
		hkConfigSet(cmd + 7, 1);
	} else if (strncmp(cmd, "set ", 4) == 0) {
		const char *a = cmd + 4;
		while (*a == ' ') a++;
		if      (!strcmp(a, "nu") || !strcmp(a, "number"))     hkConfigSet("show_line_numbers=1", 0);
		else if (!strcmp(a, "nonu") || !strcmp(a, "nonumber")) hkConfigSet("show_line_numbers=0", 0);
		else if (!strcmp(a, "rnu") || !strcmp(a, "relativenumber")) hkConfigSet("show_line_numbers=2", 0);
		else if (!strcmp(a, "wrap"))   hkConfigSet("word_wrap=1", 0);
		else if (!strcmp(a, "nowrap")) hkConfigSet("word_wrap=0", 0);
		else hkConfigSet(a, 0);
	} else if (strcmp(cmd, "theme") == 0 || strcmp(cmd, "colorscheme") == 0) {
		editorPopupOpenThemes();
	} else if (strncmp(cmd, "theme ", 6) == 0 || strncmp(cmd, "colorscheme ", 12) == 0) {
		const char *name = strchr(cmd, ' ');
		if (name) {
			while (*name == ' ') name++;
			if (editorApplyThemeByName(name)) hkSaveTheme(name);
			else editorSetStatusMessage("unknown theme: %s", name);
		}
	} else if (strncmp(cmd, "open ", 5) == 0 || strncmp(cmd, "e ", 2) == 0 || strncmp(cmd, "edit ", 5) == 0) {
		const char *filename = strchr(cmd, ' ');
		while (*filename == ' ') filename++;
		if (*filename) hkOpenPath(filename);
		else editorSetStatusMessage("Usage: :e <filename>");
	} else if (strncmp(cmd, "s/", 2) == 0 || strncmp(cmd, "%s/", 3) == 0 ||
			   strncmp(cmd, ".s/", 3) == 0) {
		if (!pane || pane->type != PANE_EDITOR) {
			editorSetStatusMessage("Not in editor pane");
			free(cmd);
			return;
		}
		int all_lines = (cmd[0] == '%');
		char *p = cmd + (all_lines ? 3 : (cmd[0] == '.' ? 3 : 2));
		char delim = '/';
		char *pat = p;
		char *rep = strchr(pat, delim);
		if (!rep) {
			editorSetStatusMessage("Usage: :s/pat/rep/[g]");
			free(cmd);
			return;
		}
		*rep++ = '\0';
		char *flags = strchr(rep, delim);
		if (flags) { *flags++ = '\0'; } else flags = "";
		int global = strchr(flags, 'g') != NULL;
		int plen = strlen(pat);
		int rlen = strlen(rep);
		if (plen == 0) {
			editorSetStatusMessage("Empty pattern");
			free(cmd);
			return;
		}

		editorSaveState();
		int start_y = all_lines ? 0 : pane->cy;
		int end_y = all_lines ? pane->numrows - 1 : pane->cy;
		int total_subs = 0;
		for (int y = start_y; y <= end_y && y < pane->numrows; y++) {
			erow *row = &pane->row[y];
			int i = 0;
			while (i <= row->size - plen) {
				if (memcmp(&row->chars[i], pat, plen) == 0) {
					int new_size = row->size - plen + rlen;
					char *nb = malloc(new_size + 1);
					memcpy(nb, row->chars, i);
					memcpy(nb + i, rep, rlen);
					memcpy(nb + i + rlen, row->chars + i + plen, row->size - i - plen);
					nb[new_size] = '\0';
					free(row->chars);
					row->chars = nb;
					row->size = new_size;
					editorUpdateRow(row);
					pane->dirty++;
					total_subs++;
					if (!global) break;
					i += rlen;
				} else {
					i++;
				}
			}
		}
		editorSetStatusMessage("%d substitution%s", total_subs, total_subs == 1 ? "" : "s");
	} else if (strcmp(cmd, "reg") == 0 || strcmp(cmd, "registers") == 0) {
		char *items[27]; int n = 0;
		char buf[160];
		for (int i = 0; i < 27; i++) {
			pasteBuffer *rg = &E.hk.registers[i];
			if (!rg->data || rg->len <= 0) continue;
			char name = (i < 26) ? ('a' + i) : '"';
			char snip[64]; int s = 0;
			for (int j = 0; j < rg->len && s < (int)sizeof(snip) - 1; j++) {
				char ch = rg->data[j];
				snip[s++] = (ch == '\n' || ch == '\t') ? ' ' : ch;
			}
			snip[s] = '\0';
			snprintf(buf, sizeof(buf), "\"%c  %5d  %s", name, rg->len, snip);
			items[n++] = strdup(buf);
		}
		if (n == 0) {
			editorSetStatusMessage("No registers set");
		} else {
			editorPopupOpen("Registers", (const char *const *)items, n, 0, 0);
			for (int i = 0; i < n; i++) free(items[i]);
		}
	} else if (cmd[0] == '!') {
		hkShellPopup(cmd + 1);
	} else if (strcmp(cmd, "run") == 0 || strncmp(cmd, "run ", 4) == 0) {
		const char *rc = cmd[3] ? cmd + 4 : "";
		while (*rc == ' ') rc++;
		if (*rc) runnerStart(rc);
		else if (E.run_cmd && *E.run_cmd) runnerStart(E.run_cmd);
		else editorSetStatusMessage("Usage: :run <cmd> (or set run_cmd in .hakorc)");
	} else if (strcmp(cmd, "build") == 0 || strcmp(cmd, "make") == 0) {
		runnerStart(E.build_cmd && *E.build_cmd ? E.build_cmd : "make");
	} else if (strcmp(cmd, "test") == 0) {
		runnerStart(E.test_cmd && *E.test_cmd ? E.test_cmd : "make test");
	} else {
		editorSetStatusMessage("Unknown command: %s", cmd);
	}
	free(cmd);
}

/*** main drawing functions ***/
void editorDrawRows(struct abuf *ab) {
	(void)ab;
	if (E.left_panel && E.left_panel->width > 0) {
		editorDrawPane(E.left_panel, ab);
		int border_x = E.left_panel->x + E.left_panel->width;
		for (int y = 0; y < E.screenrows; y++) {
			gridPutStr(border_x, y, "│", 1, E.theme.border, E.theme.border);
		}
	}

	if (E.right_panel && E.right_panel->width > 0) {
		editorDrawPane(E.right_panel, ab);
	}

	if (E.bottom_panel && E.bottom_panel->height > 0) {
		editorDrawPane(E.bottom_panel, ab);
	}

	if (E.root_pane) {
		editorDrawPane(E.root_pane, ab);
	}
}

static const char *hkKeymodeName(void) {
	return E.keymode == HK_KEYS_EMACS ? "emacs" : E.keymode == HK_KEYS_NANO ? "nano" : "";
}

/* What the editor pane under the cursor says about itself: name, modified,
   filetype, position, key mode. Shared by the status bar and the ruler. */
static void hkPaneInfo(editorPane *pane, char *left, size_t lsz, char *right, size_t rsz, int with_name) {
	left[0] = right[0] = '\0';
	const char *km = hkKeymodeName();
	if (pane && pane->type == PANE_EDITOR) {
		const char *base = hkBaseName(pane->filename);
		if (!*base) base = "[No Name]";
		if (with_name) snprintf(left, lsz, " %.40s%s", base, pane->dirty ? " [+]" : "");
		else snprintf(left, lsz, "%s", pane->dirty ? " [+]" : "");
		snprintf(right, rsz, "%s%s%s  %d,%d ", km, *km ? "  " : "",
			pane->syntax ? pane->syntax->filetype : "text", pane->cy + 1, pane->cx + 1);
	} else if (pane && pane->type == PANE_EXPLORER) {
		explorerData *data = pane->explorer;
		snprintf(left, lsz, " %s: %.60s", E.explorer_name, data && data->current_dir ? data->current_dir : "/");
		if (data && data->num_entries > 0 && data->selected >= 0 && data->selected < data->num_entries)
			snprintf(right, rsz, "%d/%d ", data->selected + 1, data->num_entries);
	} else if (pane && pane->type == PANE_AI) {
		aiData *data = pane->ai;
		snprintf(left, lsz, " %s: %s", E.ai_name, data && data->mode == MODE_INSERT ? "typing" : "ready");
		if (data && data->history_count > 0) snprintf(right, rsz, "%d lines ", data->history_count);
	} else if (pane && pane->type == PANE_RUNNER && pane->runner) {
		runnerData *r = pane->runner;
		snprintf(left, lsz, " %s %.40s %s", r->console ? "$" : "\xe2\x96\xb6",
			r->console ? (r->running ? r->cmd : "console") : r->cmd, r->running ? "[running]" : "");
		if (r->count > 0) snprintf(right, rsz, "%d/%d ", r->selected + 1, r->count);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	(void)ab;
	if (!E.status_shown) return;
	int gy = E.screenrows;
	Color sb_bg = E.theme.status_bg;
	Color sb_fg = E.theme.status_fg;

	for (int i = 0; i < E.screencols; i++) {
		gridPutCh(i, gy, ' ', sb_fg, sb_bg);
	}

	char status[128], rstatus[128];
	editorPane *pane = E.active_pane;
	/* the name is in the pane's title bar when it has one; not doubled here */
	hkPaneInfo(pane, status, sizeof(status), rstatus, sizeof(rstatus), !hkPaneHdr(pane));
	int len = (int)strlen(status), rlen = (int)strlen(rstatus);
	if (len > E.screencols) len = E.screencols;
	if (len > 0) gridPutStr(0, gy, status, len, sb_fg, sb_bg);
	if (rlen > 0 && rlen <= E.screencols - len) {
		gridPutStr(E.screencols - rlen, gy, rstatus, rlen, sb_fg, sb_bg);
	}
}

static void hkDrawNanoBar(int gy);

void editorDrawMessageBar(struct abuf *ab) {
	(void)ab;
	int gy = E.screenrows + E.status_shown;
	Color msg_bg = E.theme.bg;           /* a fixed dark grey hid the text on every light theme */
	Color msg_fg = E.theme.fg;

	for (int i = 0; i < E.screencols; i++) {
		gridPutCh(i, gy, ' ', msg_fg, msg_bg);
	}

	/* No status bar (one window, the vim default): the ruler sits at the right. */
	int rlen = 0;
	if (!E.status_shown) {
		char l[128], r[128], ruler[256];
		hkPaneInfo(E.active_pane, l, sizeof(l), r, sizeof(r), 1);
		snprintf(ruler, sizeof(ruler), "%s   %s", l + (l[0] == ' '), r);
		rlen = utf8_display_width(ruler);
		if (rlen < E.screencols / 2 + 8 && rlen < E.screencols) gridPutStr(E.screencols - rlen, gy, ruler, rlen, E.theme.line_number, msg_bg);
		else rlen = 0;
	}

	int room = E.screencols - (rlen ? rlen + 1 : 0);
	int msglen = strlen(E.statusmsg);
	if (msglen > room) msglen = room;
	if (msglen > 0 && time(NULL) - E.statusmsg_time < 5) {
		gridPutStr(0, gy, E.statusmsg, msglen, msg_fg, msg_bg);
	} else if (E.keymode == HK_KEYS_VIM && E.active_pane && E.active_pane->type == PANE_EDITOR) {
		const char *mode_str = "";
		switch (E.mode) {
		case MODE_NORMAL: mode_str = ""; break;   /* vim says nothing in normal mode */
		case MODE_INSERT: mode_str = "-- INSERT --"; break;
		case MODE_VISUAL: mode_str = "-- VISUAL --"; break;
		case MODE_VISUAL_LINE: mode_str = "-- VISUAL LINE --"; break;
		}
		Color bold = msg_fg;
		bold.a |= HK_A_BOLD;
		gridPutStr(0, gy, mode_str, room, bold, msg_bg);
	}
	if (E.keymode == HK_KEYS_NANO) hkDrawNanoBar(gy + 1);
}

static int aiPromptSegs(aiData *data, int seg_w, int **out_start, int **out_end) {
	int pmax = 32, n = 0;
	int *pstart = malloc(sizeof(int) * pmax);
	int *pend = malloc(sizeof(int) * pmax);
	if (data->prompt_buffer && data->prompt_len > 0) {
		int i = 0;
		while (i <= data->prompt_len) {
			int line_start = i;
			while (i < data->prompt_len && data->prompt_buffer[i] != '\n') i++;
			int line_end = i;
			int seg = line_start;
			do {
				int take = (line_end - seg > seg_w) ? seg_w : (line_end - seg);
				if (n >= pmax) {
					pmax *= 2;
					pstart = realloc(pstart, sizeof(int) * pmax);
					pend = realloc(pend, sizeof(int) * pmax);
				}
				pstart[n] = seg;
				pend[n] = seg + take;
				n++;
				seg += take;
			} while (seg < line_end);
			if (line_end == data->prompt_len && (data->prompt_len == 0 || data->prompt_buffer[data->prompt_len - 1] != '\n')) break;
			i++;
			if (i == data->prompt_len + 1) break;
		}
	}
	if (n == 0) {
		pstart[0] = 0;
		pend[0] = 0;
		n = 1;
	}
	*out_start = pstart;
	*out_end = pend;
	return n;
}

void editorRefreshScreen() {
#ifndef _WIN32
	if (winch_received) {
		winch_received = 0;
		editorUpdateWindowSize();
		editorResizePanes();
		E.full_redraw_pending = 1;
	}
#endif
	editorUpdateWindowSize();

	if (E.show_line_numbers) {
		editorPane **all_leaves = NULL;
		int leaf_count = 0;
		editorCollectLeafPanes(E.root_pane, &all_leaves, &leaf_count);
		int max_line = 0;
		for (int i = 0; i < leaf_count; i++) {
			if (all_leaves[i] && all_leaves[i]->type == PANE_EDITOR
				&& all_leaves[i]->numrows > max_line) {
				max_line = all_leaves[i]->numrows;
			}
		}
		free(all_leaves);
		if (max_line < 1) max_line = 1;
		E.line_number_width = 2;
		int n = max_line;
		while (n >= 10) { E.line_number_width++; n /= 10; }
		if (E.line_number_width < 4) E.line_number_width = 4;
	} else {
		E.line_number_width = 0;
	}

	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	if (E.full_redraw_pending) {
		abAppend(&ab, "\x1b[2J", 4);
		abAppend(&ab, "\x1b[H", 3);
		gridInvalidateFront();
		E.full_redraw_pending = 0;
	}

	gridClear(E.theme.bg);
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	if (E.popup_active) editorDrawPopup();
	gridFlush(&ab);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		int cursor_x = pane->x + E.line_number_width + 1;
		int cursor_y = pane->y + 1;

		if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
			int wrap_width = hkWrapWidth(pane);

			int visual_row = hkVisualRowOfLine(pane, pane->cy, wrap_width);
			int rx = editorRowCxToRx(&pane->row[pane->cy], pane->cx);
			int line_in_wrap, col_in_line;
			hkWrapPos(&pane->row[pane->cy], wrap_width, rx, &line_in_wrap, &col_in_line);
			visual_row += line_in_wrap;
			
			cursor_y = pane->y + hkPaneHdr(pane) + (visual_row - pane->rowoff) + 1;
			cursor_x = pane->x + E.line_number_width + col_in_line + 1;
		} else {
			cursor_y = pane->y + hkPaneHdr(pane) + (pane->cy - pane->rowoff) + 1;
			cursor_x = pane->x + (pane->rx - pane->coloff) + E.line_number_width + 1;
		}

		if (cursor_x < pane->x + 1) cursor_x = pane->x + 1;
		if (cursor_x > pane->x + pane->width) cursor_x = pane->x + pane->width;
		if (cursor_y < pane->y + hkPaneHdr(pane) + 1) cursor_y = pane->y + hkPaneHdr(pane) + 1;
		if (cursor_y > pane->y + pane->height) cursor_y = pane->y + pane->height;

		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
		abAppend(&ab, buf, strlen(buf));
	} else if (pane && pane->type == PANE_EXPLORER && pane->explorer) {
		explorerData *edata = pane->explorer;
		int row = pane->y + 2 + (edata->selected - edata->scroll_offset) + 1;
		int col = pane->x + 2;
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
		abAppend(&ab, buf, strlen(buf));
	} else if (pane && pane->type == PANE_RUNNER && pane->runner && pane->runner->console) {
		runnerData *r = pane->runner;
		int col = pane->x + 2 + utf8_display_width(r->input) - (r->input_len > 0 ? utf8_display_width(r->input + r->input_cur) : 0);
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", pane->y + pane->height, col + 1);
		abAppend(&ab, buf, strlen(buf));
	} else if (pane && pane->type == PANE_AI && pane->ai) {
		aiData *adata = pane->ai;
		int inner_w = pane->width - 1 - 2;
		if (inner_w < 4) inner_w = 4;
		int seg_w = inner_w - 2;
		if (seg_w < 2) seg_w = 2;

		int *pstart, *pend;
		int total_vlines = aiPromptSegs(adata, seg_w, &pstart, &pend);
		int pcur = adata->prompt_cursor;
		if (pcur < 0) pcur = 0;
		if (pcur > adata->prompt_len) pcur = adata->prompt_len;
		int vline = 0;
		int col_in_line = 0;
		for (int s = 0; s < total_vlines; s++) {
			if (pcur >= pstart[s] && pcur <= pend[s]) {
				vline = s;
				col_in_line = pcur - pstart[s];
			}
		}
		if (adata->prompt_len > 0 && adata->prompt_buffer[adata->prompt_len - 1] == '\n' && pcur == adata->prompt_len) {
			vline = total_vlines;
			col_in_line = 0;
			total_vlines++;
		}
		free(pstart);
		free(pend);

		int plines = total_vlines > 5 ? 5 : total_vlines;
		int pscroll = total_vlines > plines ? total_vlines - plines : 0;
		int prompt_rows = 1 + plines + 1;
		int history_end = pane->height - prompt_rows - 1;
		if (history_end < 2) history_end = 2;

		int prow = vline - pscroll;
		if (prow < 0) prow = 0;
		if (prow >= plines) prow = plines - 1;

		int row = pane->y + history_end + 2 + prow + 1;
		int col = pane->x + 1 + 1 + 2 + col_in_line + 1;
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
		abAppend(&ab, buf, strlen(buf));
	}

	if (!E.popup_active) abAppend(&ab, "\x1b[?25h", 6);

	if (write(STDOUT_FILENO, ab.b, ab.len) < 0) {}
	abFree(&ab);
}

/*** input processing ***/
static int hkReadKeyBlocking(void) {
	int k;
	do { k = editorReadKey(); }
	while (k == MOUSE_MOTION || k == MOUSE_CLICK || k == MOUSE_WHEEL_UP || k == MOUSE_WHEEL_DOWN || k == 0);
	return k;
}

static int hkConsumeCount(void) {
	int n = E.hk.pending_count;
	E.hk.pending_count = 0;
	return n > 0 ? n : 1;
}

static void hkRecordStart(char op, int count) {
	E.hk.last_op = op;
	E.hk.last_count = count;
	free(E.hk.last_insert);
	E.hk.last_insert = NULL;
	E.hk.last_insert_len = 0;
	E.hk.recording_insert = 1;
}

static void hkRecordChar(int c) {
	if (!E.hk.recording_insert) return;
	E.hk.last_insert = realloc(E.hk.last_insert, E.hk.last_insert_len + 2);
	if (!E.hk.last_insert) return;
	E.hk.last_insert[E.hk.last_insert_len++] = (char)c;
	E.hk.last_insert[E.hk.last_insert_len] = '\0';
}

static void hkRecordBackspace(void) {
	if (!E.hk.recording_insert) return;
	if (E.hk.last_insert_len > 0) E.hk.last_insert_len--;
	else {
		E.hk.last_insert = realloc(E.hk.last_insert, E.hk.last_insert_len + 2);
		if (E.hk.last_insert) E.hk.last_insert[E.hk.last_insert_len++] = 0x08;
	}
	if (E.hk.last_insert) E.hk.last_insert[E.hk.last_insert_len] = '\0';
}

static void hkRecordStop(void) {
	E.hk.recording_insert = 0;
}

static void hkReplayInsert(void) {
	if (!E.hk.last_insert) return;
	for (int i = 0; i < E.hk.last_insert_len; i++) {
		char c = E.hk.last_insert[i];
		if (c == '\n') editorInsertNewLine();
		else if (c == 0x08) editorDelChar();
		else editorInsertChar((unsigned char)c);
	}
}

static void hkYankLinesToReg(editorPane *pane, int n, int is_yank) {
	if (pane->cy >= pane->numrows) return;
	int end = pane->cy + n;
	if (end > pane->numrows) end = pane->numrows;
	int total = 0;
	for (int k = pane->cy; k < end; k++) total += pane->row[k].size + 1;
	char *buf = malloc(total + 1);
	if (!buf) return;
	int off = 0;
	for (int k = pane->cy; k < end; k++) {
		memcpy(buf + off, pane->row[k].chars, pane->row[k].size);
		off += pane->row[k].size;
		buf[off++] = '\n';
	}
	buf[off] = '\0';
	hkSetRegister(E.hk.pending_reg, buf, off, 1, is_yank);
	free(buf);
}

static void hkDeleteToEOL(editorPane *pane) {
	if (pane->cy >= pane->numrows) return;
	editorSaveState();
	erow *row = &pane->row[pane->cy];
	if (pane->cx < row->size) {
		hkSetRegister(E.hk.pending_reg, &row->chars[pane->cx], row->size - pane->cx, 0, 0);
		row->size = pane->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
		pane->dirty++;
	}
}

static void hkDeleteWordForward(editorPane *pane) {
	if (pane->cy >= pane->numrows) return;
	erow *row = &pane->row[pane->cy];
	int start = pane->cx;
	int end = pane->cx;
	while (end < row->size && !is_separator(row->chars[end])) end++;
	while (end < row->size && is_separator(row->chars[end]) && row->chars[end] != '\n') end++;
	if (end > start) {
		hkSetRegister(E.hk.pending_reg, &row->chars[start], end - start, 0, 0);
		memmove(&row->chars[start], &row->chars[end], row->size - end + 1);
		row->size -= (end - start);
		editorUpdateRow(row);
		pane->dirty++;
		if (pane->cx >= row->size && pane->cx > 0) pane->cx = row->size > 0 ? row->size - 1 : 0;
	}
}

static void hkJoinLines(editorPane *pane, int n) {
	for (int k = 0; k < n; k++) {
		if (pane->cy >= pane->numrows - 1) break;
		pane->cx = pane->row[pane->cy].size;
		if (pane->row[pane->cy].size > 0 && pane->row[pane->cy + 1].size > 0) editorInsertChar(' ');
		editorRowAppendString(&pane->row[pane->cy], pane->row[pane->cy + 1].chars, pane->row[pane->cy + 1].size);
		editorDelRow(pane->cy + 1);
	}
}

static void hkWordForward(editorPane *pane) {
	if (pane->cy >= pane->numrows) return;
	erow *row = &pane->row[pane->cy];
	while (pane->cx < row->size && !is_separator(row->chars[pane->cx])) pane->cx++;
	while (pane->cx < row->size && is_separator(row->chars[pane->cx])) pane->cx++;
	if (pane->cx >= row->size && pane->cy < pane->numrows - 1) {
		pane->cy++;
		pane->cx = 0;
	}
}

static void hkWordBack(editorPane *pane) {
	if (pane->cx == 0 && pane->cy == 0) return;
	if (pane->cx == 0 && pane->cy > 0) {
		pane->cy--;
		pane->cx = pane->row[pane->cy].size;
		return;
	}
	erow *row = &pane->row[pane->cy];
	while (pane->cx > 0 && is_separator(row->chars[pane->cx - 1])) pane->cx--;
	while (pane->cx > 0 && !is_separator(row->chars[pane->cx - 1])) pane->cx--;
}

static void hkDotRepeat(void) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;
	if (E.hk.last_op == 0) {
		editorSetStatusMessage("Nothing to repeat");
		return;
	}
	int n = E.hk.last_count > 0 ? E.hk.last_count : 1;
	editorSaveState();
	switch (E.hk.last_op) {
	case 'i':
	case 'I':
		hkReplayInsert();
		break;
	case 'a':
		if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size)
			pane->cx = utf8_next_char(pane->row[pane->cy].chars, pane->cx, pane->row[pane->cy].size);
		hkReplayInsert();
		break;
	case 'A':
		if (pane->cy < pane->numrows) pane->cx = pane->row[pane->cy].size;
		hkReplayInsert();
		break;
	case 'o':
		if (pane->cy < pane->numrows) pane->cx = pane->row[pane->cy].size;
		editorInsertNewLine();
		hkReplayInsert();
		break;
	case 'O':
		pane->cx = 0;
		editorInsertRow(pane->cy, "", 0);
		hkReplayInsert();
		break;
	case 'x':
		for (int k = 0; k < n; k++) {
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				editorRowDelChar(&pane->row[pane->cy], pane->cx);
				if (pane->cx >= pane->row[pane->cy].size && pane->cx > 0) pane->cx = utf8_prev_char(pane->row[pane->cy].chars, pane->cx);
			}
		}
		break;
	case 'd':
		for (int k = 0; k < n && pane->cy < pane->numrows; k++) {
			editorDelRow(pane->cy);
		}
		if (pane->numrows == 0) editorInsertRow(0, "", 0);
		if (pane->cy >= pane->numrows) pane->cy = pane->numrows - 1;
		pane->cx = 0;
		break;
	case 'J':
		hkJoinLines(pane, n);
		break;
	case 'w':
	case 'W':
		if (pane->cy < pane->numrows) {
			hkDeleteWordForward(pane);
			if (E.hk.last_op == 'W') hkReplayInsert();
		}
		break;
	}
}

static int hkTextObject(editorPane *pane, int inner, int obj, int *sx, int *ex) {
	if (pane->cy >= pane->numrows) return -1;
	erow *row = &pane->row[pane->cy];
	int cx = pane->cx;
	if (cx > row->size) cx = row->size;

	if (obj == 'w') {
		int s = cx, e = cx;
		if (s >= row->size) return -1;
		if (is_separator(row->chars[s])) {
			if (!inner) {
				while (e < row->size && is_separator(row->chars[e])) e++;
				*sx = s; *ex = e - 1;
				return 0;
			}
			while (s < row->size && is_separator(row->chars[s])) s++;
			if (s >= row->size) return -1;
			e = s;
		} else {
			while (s > 0 && !is_separator(row->chars[s - 1])) s--;
			e = s;
		}
		while (e < row->size && !is_separator(row->chars[e])) e++;
		if (!inner) {
			while (e < row->size && is_separator(row->chars[e])) e++;
		}
		*sx = s; *ex = e - 1;
		return 0;
	}

	char open = 0, close = 0;
	int is_pair = 0;
	switch (obj) {
	case '"': open = close = '"'; break;
	case '\'': open = close = '\''; break;
	case '`': open = close = '`'; break;
	case '(': case ')': open = '('; close = ')'; is_pair = 1; break;
	case '[': case ']': open = '['; close = ']'; is_pair = 1; break;
	case '{': case '}': open = '{'; close = '}'; is_pair = 1; break;
	default: return -1;
	}

	int left = -1, right = -1;
	if (is_pair) {
		int depth = 0;
		for (int i = cx; i >= 0; i--) {
			if (row->chars[i] == close) depth++;
			else if (row->chars[i] == open) {
				if (depth == 0) { left = i; break; }
				depth--;
			}
		}
		if (left < 0) return -1;
		depth = 0;
		for (int i = left + 1; i < row->size; i++) {
			if (row->chars[i] == open) depth++;
			else if (row->chars[i] == close) {
				if (depth == 0) { right = i; break; }
				depth--;
			}
		}
	} else {
		for (int i = cx; i >= 0; i--) {
			if (row->chars[i] == open) { left = i; break; }
		}
		if (left < 0) {
			for (int i = cx + 1; i < row->size; i++) {
				if (row->chars[i] == open) { left = i; break; }
			}
		}
		if (left < 0) return -1;
		for (int i = left + 1; i < row->size; i++) {
			if (row->chars[i] == close) { right = i; break; }
		}
	}
	if (right < 0) return -1;

	if (inner) { *sx = left + 1; *ex = right - 1; }
	else { *sx = left; *ex = right; }
	if (*ex < *sx) return -1;
	return 0;
}

static void hkApplyLineOp(editorPane *pane, int op, int sx, int ex) {
	erow *row = &pane->row[pane->cy];
	int len = ex - sx + 1;
	if (len <= 0) return;
	if (op == 'y') {
		hkSetRegister(E.hk.pending_reg, &row->chars[sx], len, 0, 1);
		E.hk.pending_reg = 0;
		return;
	}
	hkSetRegister(E.hk.pending_reg, &row->chars[sx], len, 0, 0);
	E.hk.pending_reg = 0;
	memmove(&row->chars[sx], &row->chars[ex + 1], row->size - ex - 1);
	row->size -= len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	pane->dirty++;
	pane->cx = sx;
	if (pane->cx > row->size) pane->cx = row->size > 0 ? row->size - 1 : 0;
	if (op == 'c') editorSetMode(MODE_INSERT);
}

void hkPushJump(int x, int y) {
	if (E.hk.jump_count >= 100) {
		free(E.hk.jumps[0].filename);
		memmove(&E.hk.jumps[0], &E.hk.jumps[1], sizeof(E.hk.jumps[0]) * 99);
		E.hk.jump_count = 99;
	}
	E.hk.jumps[E.hk.jump_count].x = x;
	E.hk.jumps[E.hk.jump_count].y = y;
	E.hk.jumps[E.hk.jump_count].filename = NULL;
	E.hk.jump_count++;
	E.hk.jump_pos = E.hk.jump_count;
}

void hkClampCursor(editorPane *pane) {
	if (pane->cy < 0) pane->cy = 0;
	if (pane->cy >= pane->numrows) pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	if (pane->cy < pane->numrows && pane->cx > pane->row[pane->cy].size) {
		pane->cx = pane->row[pane->cy].size;
	}
	if (pane->cx < 0) pane->cx = 0;
}

/*** key modes: emacs and nano ***/
/* Modeless typing for both. The mark (emacs C-SPC, nano M-A) is the visual
   selection underneath, so the region draws like one. A region runs from the
   mark to the cursor, the cursor's character not included. */
static int hk_last_kill;            /* the previous key cut text: the next cut adds to it */
static char hk_last_search[256];

static int hkRegion(editorPane *p, int *sy, int *sx, int *ey, int *ex) {
	if (E.mode != MODE_VISUAL) return 0;
	int ay = p->visual_anchor_y, ax = p->visual_anchor_x;
	if (ay > p->cy || (ay == p->cy && ax > p->cx)) { *sy = p->cy; *sx = p->cx; *ey = ay; *ex = ax; }
	else { *sy = ay; *sx = ax; *ey = p->cy; *ex = p->cx; }
	if (*ey >= p->numrows) { *ey = p->numrows - 1; *ex = p->row[*ey].size; }
	if (*sx > p->row[*sy].size) *sx = p->row[*sy].size;
	if (*ex > p->row[*ey].size) *ex = p->row[*ey].size;
	return *sy != *ey || *sx != *ex;
}

static char *hkRangeText(editorPane *p, int sy, int sx, int ey, int ex, int *len) {
	size_t total = 0;
	for (int y = sy; y <= ey; y++) total += p->row[y].size + 1;
	char *out = malloc(total + 1), *o = out;
	if (!out) { *len = 0; return NULL; }
	for (int y = sy; y <= ey; y++) {
		int a = y == sy ? sx : 0, b = y == ey ? ex : p->row[y].size;
		if (b > a) { memcpy(o, p->row[y].chars + a, b - a); o += b - a; }
		if (y < ey) *o++ = '\n';
	}
	*o = '\0';
	*len = (int)(o - out);
	return out;
}

static void hkDeleteRange(editorPane *p, int sy, int sx, int ey, int ex) {
	erow *r = &p->row[sy];
	if (sy == ey) {
		memmove(r->chars + sx, r->chars + ex, r->size - ex + 1);
		r->size -= ex - sx;
		editorUpdateRow(r);
	} else {
		erow *last = &p->row[ey];
		int tail = last->size - ex;
		char *nc = malloc(sx + tail + 1);
		if (!nc) return;
		memcpy(nc, r->chars, sx);
		memcpy(nc + sx, last->chars + ex, tail);
		nc[sx + tail] = '\0';
		free(r->chars);
		r->chars = nc;
		r->size = sx + tail;
		editorUpdateRow(r);
		for (int y = ey; y > sy; y--) editorDelRow(y);
	}
	p->dirty++;
	p->cy = sy;
	p->cx = sx;
}

/* kill or cut: into the unnamed register, after the last cut when they run on */
static void hkKill(const char *text, int len, int is_line, int append) {
	if (append && E.paste.data && E.paste.len > 0) {
		char *both = malloc(E.paste.len + len + 1);
		if (!both) return;
		memcpy(both, E.paste.data, E.paste.len);
		memcpy(both + E.paste.len, text, len);
		hkSetRegister(0, both, E.paste.len + len, is_line, 0);
		free(both);
	} else {
		hkSetRegister(0, text, len, is_line, 0);
	}
}

static void hkMarkSet(editorPane *p, int on) {
	if (on) {
		p->visual_anchor_x = p->cx;
		p->visual_anchor_y = p->cy;
		E.mode = MODE_VISUAL;
		editorSetStatusMessage("Mark set");
	} else {
		E.mode = MODE_INSERT;
	}
}

/* cut the region, or (nano ^K, emacs C-k) a line */
static void hkCutRegion(editorPane *p, int copy_only) {
	int sy, sx, ey, ex, len;
	if (!hkRegion(p, &sy, &sx, &ey, &ex)) { hkMarkSet(p, 0); return; }
	char *t = hkRangeText(p, sy, sx, ey, ex, &len);
	if (t) { hkKill(t, len, 0, 0); free(t); }
	if (!copy_only) { editorSaveState(); hkDeleteRange(p, sy, sx, ey, ex); }
	hkMarkSet(p, 0);
	editorSetStatusMessage(copy_only ? "Copied %d bytes" : "Cut %d bytes", len);
}

static void hkYank(editorPane *p) {
	pasteBuffer *pb = hkGetRegister(0);
	if (!pb || !pb->data || pb->len <= 0) { editorSetStatusMessage("Nothing to paste"); return; }
	editorSaveState();
	if (pb->is_line_mode) p->cx = 0;           /* whole lines go above this one */
	editorHandlePaste(pb->data, pb->len);
}

static void hkTypeKey(editorPane *p, int c) {
	if (E.mode == MODE_VISUAL) hkMarkSet(p, 0);   /* typing ends the region */
	editorSaveState();
	if (c == '\r') editorInsertNewLine();
	else if (c == BACKSPACE || c == CTRL_KEY('h')) editorDelChar();
	else if (c == DEL_KEY || c == CTRL_KEY('d')) {
		if (p->cy < p->numrows && (p->cx < p->row[p->cy].size || p->cy + 1 < p->numrows)) {
			editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
		}
	}
	else editorInsertChar(c);
}

static void hkPage(editorPane *p, int dir) {
	int step = p->height - hkPaneHdr(p) - 2;
	if (step < 1) step = 1;
	p->cy += dir * step;
	hkClampCursor(p);
}

/* the first match at or after (y, x) going forward, or before it going back; wraps */
static int hkSearchFrom(editorPane *p, const char *q, int y, int x, int dir, int *oy, int *ox) {
	int ql = (int)strlen(q);
	if (!ql || p->numrows == 0) return 0;
	for (int k = 0; k <= p->numrows; k++) {
		int r = ((dir > 0 ? y + k : y - k) % p->numrows + p->numrows) % p->numrows;
		erow *row = &p->row[r];
		if (dir > 0) {
			int from = k == 0 ? x : 0;
			for (int i = from; i + ql <= row->size; i++)
				if (!memcmp(row->chars + i, q, ql)) { *oy = r; *ox = i; return 1; }
		} else {
			int to = k == 0 ? x : row->size;
			for (int i = (to < row->size ? to : row->size) - ql; i >= 0; i--)
				if (!memcmp(row->chars + i, q, ql) && (k > 0 || i <= x)) { *oy = r; *ox = i; return 1; }
		}
	}
	return 0;
}

static void hkSearchShow(editorPane *p, const char *q) {
	free(E.search.query);
	E.search.query = (q && *q) ? strdup(q) : NULL;
	hkRefreshSyntax(p);
}

/* emacs C-s / C-r: moves while you type; C-s again finds the next, C-g goes back */
static void hkIsearch(editorPane *p, int dir) {
	int oy = p->cy, ox = p->cx;
	char q[256] = "";
	int qn = 0, found = 1;
	for (;;) {
		hkSearchShow(p, q);
		editorSetStatusMessage("%sI-search%s: %s", found ? "" : "Failing ", dir < 0 ? " backward" : "", q);
		editorRefreshScreen();
		int c = hkReadKeyBlocking();
		int fy, fx;
		if (c == CTRL_KEY('s') || c == CTRL_KEY('r')) {
			dir = c == CTRL_KEY('s') ? 1 : -1;
			if (!qn && hk_last_search[0]) { snprintf(q, sizeof(q), "%s", hk_last_search); qn = (int)strlen(q); }
			int sx = dir > 0 ? p->cx + 1 : p->cx - 1, sy = p->cy;
			if (sx < 0) { sy--; sx = sy >= 0 ? p->row[sy].size : 0; if (sy < 0) sy = p->numrows - 1; }
			found = hkSearchFrom(p, q, sy, sx, dir, &fy, &fx);
		} else if (c == BACKSPACE || c == CTRL_KEY('h')) {
			if (qn) q[--qn] = '\0';
			p->cy = oy; p->cx = ox;
			found = !qn || hkSearchFrom(p, q, oy, ox, dir, &fy, &fx);
		} else if (c == CTRL_KEY('g') || c == '\x1b') {
			p->cy = oy; p->cx = ox;
			hkSearchShow(p, NULL);
			editorSetStatusMessage("Quit");
			return;
		} else if (c >= 32 && c < 256 && c != 127) {
			if (qn < (int)sizeof(q) - 1) { q[qn++] = (char)c; q[qn] = '\0'; }
			found = hkSearchFrom(p, q, p->cy, dir > 0 ? p->cx : p->cx + qn, dir, &fy, &fx);
		} else {
			break;                                  /* enter, or any other key, ends it */
		}
		if (found && qn) { p->cy = fy; p->cx = fx; }
	}
	if (qn) snprintf(hk_last_search, sizeof(hk_last_search), "%s", q);
	if (p->cy != oy || p->cx != ox) hkPushJump(ox, oy);
	hkSearchShow(p, NULL);
	editorSetStatusMessage("");
}

static void hkGotoLine(editorPane *p) {
	char *a = hkPromptWith("Enter line number, column number: %s", NULL, NULL);
	if (!a) return;
	int l = atoi(a), col = 1;
	const char *comma = strchr(a, ',');
	if (comma) col = atoi(comma + 1);
	free(a);
	if (l < 1) l = 1;
	hkPushJump(p->cx, p->cy);
	p->cy = l - 1;
	p->cx = col > 0 ? col - 1 : 0;
	hkClampCursor(p);
}

static void hkPosition(editorPane *p) {
	long total = 0, before = 0;
	for (int y = 0; y < p->numrows; y++) {
		if (y < p->cy) before += p->row[y].size + 1;
		total += p->row[y].size + 1;
	}
	before += p->cx;
	int size = p->cy < p->numrows ? p->row[p->cy].size : 0;
	editorSetStatusMessage("line %d/%d (%d%%), col %d/%d, char %ld/%ld (%d%%)",
		p->cy + 1, p->numrows, p->numrows ? (p->cy + 1) * 100 / p->numrows : 0,
		p->cx + 1, size + 1, before, total, total ? (int)(before * 100 / total) : 0);
}

static int hkAnyDirty(void) {
	editorPane **leaves = NULL;
	int n = 0, dirty = 0;
	editorCollectLeafPanes(E.root_pane, &leaves, &n);
	for (int i = 0; i < n; i++) if (leaves[i]->type == PANE_EDITOR && leaves[i]->dirty) dirty = 1;
	free(leaves);
	return dirty;
}

/* one key from a y/n question in the message line */
static int hkAsk(const char *q) {
	editorSetStatusMessage("%s", q);
	editorRefreshScreen();
	int c = hkReadKeyBlocking();
	editorSetStatusMessage("");
	return c;
}

/* nano ^X, emacs C-x k: close this buffer (saving if asked), the last one quits */
static void hkCloseBuffer(editorPane *p) {
	if (p->dirty) {
		int c = hkAsk("Save modified buffer?  Y Yes  N No  ^C Cancel");
		if (c == 'y' || c == 'Y') {
			editorSave(0);
			if (p->dirty) return;
		} else if (c != 'n' && c != 'N') {
			editorSetStatusMessage("Cancelled");
			return;
		}
	}
	if (hkLeafCount() > 1) {
		E.force_window_command = 1;
		editorClosePane();
		return;
	}
	exit(0);
}

static void hkWriteOut(editorPane *p) {
	char *name = hkPromptWith("File Name to Write: %s", p->filename ? hkBaseName(p->filename) : "", NULL);
	if (!name) { editorSetStatusMessage("Cancelled"); return; }
	if (!p->filename || strcmp(hkBaseName(p->filename), name) != 0) {
		free(p->filename);
		p->filename = name;
		p->file_mtime = 0;
		editorSelectSyntaxHighlight();
		editorSave(1);
		return;
	}
	free(name);
	editorSave(0);
}

static void hkFindFile(void) {
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
	size_t l = strlen(cwd);
	if (l + 1 < sizeof(cwd)) { cwd[l] = '/'; cwd[l + 1] = '\0'; }
	char *path = hkPromptWith("Find file: %s", cwd, NULL);
	if (!path) return;
	hkOpenPath(path);
	free(path);
}

/* nano ^R: the file's text at the cursor */
static void hkInsertFile(editorPane *p) {
	char *path = hkPromptWith("File to insert: %s", NULL, NULL);
	if (!path) return;
	FILE *fp = fopen(path, "r");
	if (!fp) { editorSetStatusMessage("%s: %s", path, strerror(errno)); free(path); return; }
	char *buf = NULL;
	size_t n = 0, cap = 0, got;
	char chunk[4096];
	while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		if (n + got + 1 > cap) { cap = (n + got + 1) * 2; char *nb = realloc(buf, cap); if (!nb) break; buf = nb; }
		memcpy(buf + n, chunk, got);
		n += got;
	}
	fclose(fp);
	if (buf) { editorHandlePaste(buf, (int)n); free(buf); }
	editorSetStatusMessage("Read %zu bytes from %s", n, path);
	free(path);
	(void)p;
}

/* nano ^\: each match from the cursor on, asked about one at a time */
static void hkReplace(editorPane *p) {
	char *needle = hkPromptWith("Search (to replace): %s", hk_last_search[0] ? hk_last_search : NULL, NULL);
	if (!needle) return;
	char *with = hkPromptWith("Replace with: %s", NULL, NULL);
	if (!with) with = strdup("");
	snprintf(hk_last_search, sizeof(hk_last_search), "%s", needle);
	int nl = (int)strlen(needle), wl = (int)strlen(with), count = 0, all = 0;
	int y = p->cy, x = p->cx;
	editorSaveState();
	while (y < p->numrows) {
		erow *row = &p->row[y];
		char *hit = NULL;
		for (int i = x; i + nl <= row->size; i++) if (!memcmp(row->chars + i, needle, nl)) { hit = row->chars + i; break; }
		if (!hit) { y++; x = 0; continue; }
		int at = (int)(hit - row->chars);
		p->cy = y; p->cx = at;
		int c = 'y';
		if (!all) {
			hkSearchShow(p, needle);
			c = hkAsk("Replace this instance?  Y Yes  N No  A All  ^C Cancel");
		}
		if (c == 'a' || c == 'A') { all = 1; c = 'y'; }
		if (c == 'y' || c == 'Y') {
			char *nb = malloc(row->size - nl + wl + 1);
			if (!nb) break;
			memcpy(nb, row->chars, at);
			memcpy(nb + at, with, wl);
			memcpy(nb + at + wl, row->chars + at + nl, row->size - at - nl + 1);
			free(row->chars);
			row->chars = nb;
			row->size += wl - nl;
			editorUpdateRow(row);
			p->dirty++;
			count++;
			x = at + wl;
		} else if (c == 'n' || c == 'N') {
			x = at + 1;
		} else {
			break;
		}
	}
	hkSearchShow(p, NULL);
	editorSetStatusMessage("Replaced %d occurrence%s", count, count == 1 ? "" : "s");
	free(needle);
	free(with);
}

static void hkFocusLeafPick(int sel) { editorFocusEditorN(sel + 1); }

/* emacs C-x b: the open buffers, one per editor window */
static void hkBufferList(void) {
	editorPane **leaves = NULL;
	int n = 0;
	editorCollectLeafPanes(E.root_pane, &leaves, &n);
	char **items = calloc(n ? n : 1, sizeof(char *));
	for (int i = 0; i < n; i++) {
		char line[300];
		const char *base = hkBaseName(leaves[i]->filename);
		snprintf(line, sizeof(line), "%d  %s%s", i + 1, *base ? base : "[No Name]", leaves[i]->dirty ? " [+]" : "");
		items[i] = strdup(line);
	}
	editorPopupOpen("Buffers", (const char *const *)items, n, 1, 0);
	E.popup_apply = hkFocusLeafPick;
	for (int i = 0; i < n; i++) if (leaves[i] == E.active_pane) E.popup_sel = i;
	for (int i = 0; i < n; i++) free(items[i]);
	free(items);
	free(leaves);
}

/* C-x 1: keep this window, close the others that have nothing unsaved */
static void hkOnlyWindow(editorPane *keep) {
	if (E.left_panel) editorToggleExplorer();
	if (E.right_panel) editorToggleAI();
	if (E.bottom_panel) hkCloseSidePanel(&E.bottom_panel, NULL);
	int kept = 0;
	for (int guard = 0; guard < MAX_PANES; guard++) {
		editorPane **leaves = NULL;
		int n = 0;
		editorCollectLeafPanes(E.root_pane, &leaves, &n);
		editorPane *victim = NULL;
		for (int i = 0; i < n; i++) if (leaves[i] != keep && !leaves[i]->dirty) { victim = leaves[i]; break; }
		for (int i = 0; i < n; i++) if (leaves[i] != keep && leaves[i]->dirty) kept++;
		free(leaves);
		if (!victim) break;
		kept = 0;
		E.active_pane = victim;
		editorClosePane();
	}
	E.active_pane = keep;
	keep->is_focused = 1;
	if (kept) editorSetStatusMessage("%d window%s with unsaved changes left open", kept, kept == 1 ? "" : "s");
}

/* C-x then a key; nano's and emacs's ways to the : commands. Any pane. */
static int hkModelessGlobal(int c) {
	static int cx = 0;
	editorPane *p = E.active_pane;
	if (E.keymode == HK_KEYS_NANO) {
		if (c == CTRL_KEY('t')) { editorColonCommand(); return 1; }
		return 0;
	}
	if (!cx) {
		if (c != CTRL_KEY('x')) return 0;
		cx = 1;
		editorSetStatusMessage("C-x-");
		return 1;
	}
	cx = 0;
	editorSetStatusMessage("");
	int ed = p && p->type == PANE_EDITOR;
	switch (c) {
	case CTRL_KEY('s'): if (ed) editorSave(0); break;
	case CTRL_KEY('w'): if (ed) hkWriteOut(p); break;
	case CTRL_KEY('f'): hkFindFile(); break;
	case CTRL_KEY('c'):
		if (!hkAnyDirty() || (hkAsk("Modified buffers exist; exit anyway? (y or n)") | 32) == 'y') exit(0);
		break;
	case 'b': hkBufferList(); break;
	case 'k': if (ed) hkCloseBuffer(p); else if (p) editorClosePane(); break;
	case 'o': editorNextPane(); break;
	case '0': editorClosePane(); break;
	case '1': if (ed) hkOnlyWindow(p); break;
	case '2': editorSplitPane(0); break;
	case '3': editorSplitPane(1); break;
	case 'u': if (ed) editorUndo(); break;
	case 'h':
		if (ed && p->numrows) {
			p->cy = p->numrows - 1;
			p->cx = p->row[p->cy].size;
			p->visual_anchor_x = p->visual_anchor_y = 0;
			E.mode = MODE_VISUAL;
		}
		break;
	case CTRL_KEY('g'): editorSetStatusMessage("Quit"); break;
	default: editorSetStatusMessage("C-x %s%c is undefined", c < 32 ? "C-" : "", c < 32 ? c + 96 : (c < 127 ? c : '?'));
	}
	return 1;
}

static void hkEmacsKey(editorPane *p, int c) {
	static int meta = 0;
	int was_kill = hk_last_kill;
	hk_last_kill = 0;
	if (meta) {
		meta = 0;
		switch (c) {
		case 'x': case 'X': editorColonCommand(); break;
		case 'f': hkWordForward(p); break;
		case 'b': hkWordBack(p); break;
		case '<': hkPushJump(p->cx, p->cy); p->cy = 0; p->cx = 0; break;
		case '>': hkPushJump(p->cx, p->cy); p->cy = p->numrows ? p->numrows - 1 : 0; p->cx = p->numrows ? p->row[p->cy].size : 0; break;
		case 'v': hkPage(p, -1); break;
		case 'w': hkCutRegion(p, 1); break;
		case 'd': editorSaveState(); hkDeleteWordForward(p); break;
		case '%': hkReplace(p); break;
		case 'g': hkGotoLine(p); break;
		default: break;
		}
		return;
	}
	switch (c) {
	case '\x1b': meta = 1; return;
	case CTRL_KEY('g'):
		if (E.mode == MODE_VISUAL) hkMarkSet(p, 0);
		editorSetStatusMessage("Quit");
		break;
	case CTRL_KEY('f'): case ARROW_RIGHT: editorMoveCursor(ARROW_RIGHT); break;
	case CTRL_KEY('b'): case ARROW_LEFT:  editorMoveCursor(ARROW_LEFT); break;
	case CTRL_KEY('n'): case ARROW_DOWN:  editorMoveCursor(ARROW_DOWN); break;
	case CTRL_KEY('p'): case ARROW_UP:    editorMoveCursor(ARROW_UP); break;
	case CTRL_KEY('a'): case HOME_KEY: p->cx = 0; break;
	case CTRL_KEY('e'): case END_KEY: if (p->cy < p->numrows) p->cx = p->row[p->cy].size; break;
	case CTRL_KEY('v'): case PAGE_DOWN: hkPage(p, 1); break;
	case PAGE_UP: hkPage(p, -1); break;
	case CTRL_KEY('l'): break;
	case CTRL_SPACE_KEY: hkMarkSet(p, 1); break;
	case CTRL_KEY('w'): hkCutRegion(p, 0); break;
	case CTRL_KEY('y'): hkYank(p); break;
	case CTRL_KEY('s'): hkIsearch(p, 1); break;
	case CTRL_KEY('r'): hkIsearch(p, -1); break;
	case 0x1f: editorUndo(); break;                     /* C-/ and C-_ */
	case CTRL_KEY('k'):
		if (p->cy >= p->numrows) break;
		editorSaveState();
		if (p->cx < p->row[p->cy].size) {
			int len = p->row[p->cy].size - p->cx;
			hkKill(p->row[p->cy].chars + p->cx, len, 0, was_kill);
			hkDeleteRange(p, p->cy, p->cx, p->cy, p->row[p->cy].size);
		} else if (p->cy + 1 < p->numrows) {
			hkKill("\n", 1, 0, was_kill);
			hkDeleteRange(p, p->cy, p->cx, p->cy + 1, 0);
		}
		hk_last_kill = 1;
		break;
	case CTRL_KEY('o'):
		editorSaveState();
		editorInsertNewLine();
		editorMoveCursor(ARROW_LEFT);
		break;
	case '\t': case '\r': case BACKSPACE: case CTRL_KEY('h'): case CTRL_KEY('d'): case DEL_KEY:
		hkTypeKey(p, c);
		break;
	default:
		if (c >= 32 && c < 256 && c != 127) hkTypeKey(p, c);
		break;
	}
}

static void hkNanoKey(editorPane *p, int c) {
	static int meta = 0;
	int was_cut = hk_last_kill;
	hk_last_kill = 0;
	if (meta) {
		meta = 0;
		switch (c) {
		case 'u': case 'U': editorUndo(); break;
		case 'e': case 'E': editorRedo(); break;
		case 'a': case 'A': hkMarkSet(p, E.mode != MODE_VISUAL); break;
		case '6': case '^': hkCutRegion(p, 1); break;
		case 'w': case 'W':
			if (hk_last_search[0]) {
				int fy, fx;
				if (hkSearchFrom(p, hk_last_search, p->cy, p->cx + 1, 1, &fy, &fx)) { p->cy = fy; p->cx = fx; }
				else editorSetStatusMessage("\"%s\" not found", hk_last_search);
			}
			break;
		case '>': case '.': editorNextPane(); break;
		case '<': case ',': editorCyclePane(-1); break;
		case '\\': p->cy = 0; p->cx = 0; break;
		case '/': p->cy = p->numrows ? p->numrows - 1 : 0; break;
		case 'g': case 'G': hkGotoLine(p); break;
		case 'r': case 'R': hkReplace(p); break;
		default: break;
		}
		return;
	}
	switch (c) {
	case '\x1b': meta = 1; return;
	case CTRL_KEY('o'): hkWriteOut(p); break;
	case CTRL_KEY('s'): editorSave(0); break;
	case CTRL_KEY('x'): hkCloseBuffer(p); break;
	case CTRL_KEY('g'): editorPopupOpenHelp(); break;
	case CTRL_KEY('c'): hkPosition(p); break;
	case 0x1f: hkGotoLine(p); break;                       /* ^_ */
	case CTRL_KEY('r'): hkInsertFile(p); break;
	case 0x1c: hkReplace(p); break;                        /* ^\ */
	case 0x1e: hkMarkSet(p, E.mode != MODE_VISUAL); break; /* ^^ */
	case CTRL_KEY('w'): {
		char *q = hkPromptWith("Search: %s", NULL, NULL);
		if (!q) break;
		snprintf(hk_last_search, sizeof(hk_last_search), "%s", q);
		int fy, fx;
		if (hkSearchFrom(p, q, p->cy, p->cx + 1, 1, &fy, &fx)) { hkPushJump(p->cx, p->cy); p->cy = fy; p->cx = fx; }
		else editorSetStatusMessage("\"%s\" not found", q);
		free(q);
		break;
	}
	case CTRL_KEY('k'):
		if (E.mode == MODE_VISUAL) { hkCutRegion(p, 0); break; }
		if (p->cy >= p->numrows) break;
		editorSaveState();
		{
			erow *r = &p->row[p->cy];
			char *line = malloc(r->size + 2);
			if (!line) break;
			memcpy(line, r->chars, r->size);
			line[r->size] = '\n';
			hkKill(line, r->size + 1, 1, was_cut);
			free(line);
			if (p->numrows > 1) editorDelRow(p->cy);
			else { r->size = 0; r->chars[0] = '\0'; editorUpdateRow(r); p->dirty++; }
			hkClampCursor(p);
			p->cx = 0;
		}
		hk_last_kill = 1;
		break;
	case CTRL_KEY('u'): hkYank(p); break;
	case CTRL_KEY('a'): case HOME_KEY: p->cx = 0; break;
	case CTRL_KEY('e'): case END_KEY: if (p->cy < p->numrows) p->cx = p->row[p->cy].size; break;
	case CTRL_KEY('y'): case PAGE_UP: hkPage(p, -1); break;
	case CTRL_KEY('v'): case PAGE_DOWN: hkPage(p, 1); break;
	case CTRL_KEY('f'): case ARROW_RIGHT: editorMoveCursor(ARROW_RIGHT); break;
	case CTRL_KEY('b'): case ARROW_LEFT:  editorMoveCursor(ARROW_LEFT); break;
	case CTRL_KEY('n'): case ARROW_DOWN:  editorMoveCursor(ARROW_DOWN); break;
	case CTRL_KEY('p'): case ARROW_UP:    editorMoveCursor(ARROW_UP); break;
	case CTRL_KEY('l'): break;
	case '\t': case '\r': case BACKSPACE: case CTRL_KEY('h'): case CTRL_KEY('d'): case DEL_KEY:
		hkTypeKey(p, c);
		break;
	default:
		if (c >= 32 && c < 256 && c != 127) hkTypeKey(p, c);
		break;
	}
}

/* nano's two rows of shortcuts */
static void hkDrawNanoBar(int gy) {
	static const char *const rows[2][6][2] = {
		{{"^G", "Help"}, {"^O", "Write Out"}, {"^W", "Where Is"}, {"^K", "Cut"}, {"^T", "Command"}, {"^C", "Location"}},
		{{"^X", "Exit"}, {"^R", "Read File"}, {"^\\", "Replace"}, {"^U", "Paste"}, {"^_", "Go To Line"}, {"M-U", "Undo"}},
	};
	int w = E.screencols / 6;
	if (w < 6) w = 6;
	Color key_fg = E.theme.status_fg, key_bg = E.theme.status_bg;
	for (int r = 0; r < 2; r++) {
		int y = gy + r;
		for (int x = 0; x < E.screencols; x++) gridPutCh(x, y, ' ', E.theme.fg, E.theme.bg);
		for (int k = 0; k < 6 && k * w < E.screencols; k++) {
			int x = k * w;
			int kw = gridPutStr(x, y, rows[r][k][0], E.screencols - x, key_fg, key_bg);
			if (x + kw + 1 < E.screencols)
				gridPutStr(x + kw + 1, y, rows[r][k][1], MIN(w - kw - 2, E.screencols - x - kw - 1), E.theme.fg, E.theme.bg);   /* one cell always free before the next key */
		}
	}
}

void editorProcessKeyPress() {
	static int last_char = 0;
	static int g_pressed = 0;
	static int d_pressed = 0;
	static time_t last_undo_time = 0;
	static int last_action = 0;

	int c = editorReadKey();

	if (E.intro_active && c != 0 && c != MOUSE_MOTION && c != CTRL_KEY('l')) E.intro_active = 0;

	if (E.popup_active) {
		editorPopupHandleKey(c);
		return;
	}

	if (c == CTRL_KEY('l')) {
		return;
	}

	if (c == MOUSE_WHEEL_UP || c == MOUSE_WHEEL_DOWN) {
		editorHandleScroll(c);
		return;
	}

	if (c == MOUSE_CLICK) {
		editorProcessMouse(E.mouse_x, E.mouse_y);
		if (E.mode == MODE_INSERT && E.keymode == HK_KEYS_VIM) editorSetMode(MODE_INSERT);
		return;
	}

	if (c == MOUSE_MOTION || c == 0) {
		return;
	}

	if (E.in_window_command) {
		E.in_window_command = 0;
		switch (c) {
		case 's':
			editorSplitPane(0);
			break;
		case 'v':
			editorSplitPane(1);
			break;
		case 'w':
		case CTRL_KEY('w'):
			editorNextPane();
			break;
		case '1': case '2': case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
			editorFocusEditorN(c - '0');
			break;
		case 'r': {                              /* Rei */
			editorPane *p = editorFindPaneOfType(PANE_AI);
			if (!p) { editorToggleAI(); p = editorFindPaneOfType(PANE_AI); }
			editorFocusPane(p);
			break;
		}
		case 'k': {                              /* Kami */
			editorPane *p = editorFindPaneOfType(PANE_EXPLORER);
			if (!p) { editorToggleExplorer(); p = editorFindPaneOfType(PANE_EXPLORER); }
			editorFocusPane(p);
			break;
		}
		case 'c':
			editorClosePane();
			break;
		case '!':
			E.force_window_command = 1;
			editorClosePane();
			break;
		case '+':
		case '-':
		case '<':
		case '>': {
			int delta_cols = (c == '+' || c == '>') ? 4 : -4;
			if (E.active_pane == E.left_panel && E.left_panel) {
				int nw = E.left_panel_width + delta_cols;
				int max_w = E.screencols - (E.right_panel ? E.right_panel_width : 0) - 10;
				if (nw >= 10 && nw <= max_w) {
					E.left_panel_width = nw;
					editorResizePanes();
				}
			} else if (E.active_pane == E.right_panel && E.right_panel) {
				int nw = E.right_panel_width - delta_cols;
				int max_w = E.screencols - (E.left_panel ? E.left_panel_width : 0) - 10;
				if (nw >= 10 && nw <= max_w) {
					E.right_panel_width = nw;
					editorResizePanes();
				}
			} else if (E.active_pane && E.active_pane->parent && E.active_pane->parent->split_dir != SPLIT_NONE) {
				float delta = (c == '+' || c == '>') ? 0.05 : -0.05;
				float new_ratio = E.active_pane->parent->split_ratio + delta;
				if (E.active_pane->parent->child2 == E.active_pane) new_ratio = E.active_pane->parent->split_ratio - delta;
				if (new_ratio >= 0.2 && new_ratio <= 0.8) {
					E.active_pane->parent->split_ratio = new_ratio;
					editorUpdatePaneBounds(E.active_pane->parent);
				}
			}
			break;
		}
		default:
			editorSetStatusMessage("Unknown window command");
			break;
		}
		return;
	}

	if (E.keymode != HK_KEYS_VIM && hkModelessGlobal(c)) return;

	/* ctrl-w is vim's window chord; emacs and nano editors use the key themselves */
	if (c == CTRL_KEY('w') && (E.keymode == HK_KEYS_VIM || !E.active_pane || E.active_pane->type != PANE_EDITOR)) {
		E.in_window_command = 1;
		editorSetStatusMessage("Window command (s/v/w/c/! · 1-9 editor · r Rei · k Kami)");
		return;
	}

	editorPane *pane = E.active_pane;

	if (pane && pane->type == PANE_EXPLORER) {
		explorerHandleKey(pane, c);
		return;
	}

	if (pane && pane->type == PANE_RUNNER) {
		runnerHandleKey(pane, c);
		return;
	}

	if (pane && pane->type == PANE_AI) {
		aiHandleKey(pane, c);
		return;
	}

	if (!pane || pane->type != PANE_EDITOR) return;

	if (E.keymode == HK_KEYS_NANO) { hkNanoKey(pane, c); return; }
	if (E.keymode == HK_KEYS_EMACS) { hkEmacsKey(pane, c); return; }

	if (E.mode == MODE_NORMAL) {
		if (getenv("HAKE_TRACE")) {
			editorSetStatusMessage("key=%d('%c') last=%d('%c') reg=%d", c,
				(c >= 32 && c < 127) ? c : '?', last_char,
				(last_char >= 32 && last_char < 127) ? last_char : '?',
				E.hk.pending_reg);
		}
		if ((c >= '1' && c <= '9') || (c == '0' && E.hk.pending_count > 0)) {
			long next = (long)E.hk.pending_count * 10 + (c - '0');
			if (next > 100000) next = 100000;
			E.hk.pending_count = (int)next;
			return;
		}

		if (E.hk.waiting_reg) {
			E.hk.waiting_reg = 0;
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				c == '"' || c == '+' || c == '*' || c == '0' || c == '_' || c == '-') {
				E.hk.pending_reg = (char)c;
				editorSetStatusMessage("reg=\"%c (key=%d)", c, c);
			} else {
				E.hk.pending_reg = 0;
				editorSetStatusMessage("register canceled (key=%d)", c);
			}
			return;
		}

		if (c == '"') {
			last_char = 0; g_pressed = 0; d_pressed = 0;
			E.hk.waiting_reg = 1;
			editorSetStatusMessage("\" (waiting for register name)");
			return;
		}

		if ((last_char == 'd' || last_char == 'c' || last_char == 'y') && (c == 'i' || c == 'a')) {
			int op = last_char;
			int inner = (c == 'i');
			int obj = hkReadKeyBlocking();
			int sx, ex;
			if (hkTextObject(pane, inner, obj, &sx, &ex) == 0) {
				editorSaveState();
				hkApplyLineOp(pane, op, sx, ex);
			} else {
				editorSetStatusMessage("No text object: %c%c%c at %d,%d (cursor must be inside)", op, c, (obj >= 32 && obj < 127) ? obj : '?', pane->cx, pane->cy);
			}
			last_char = 0; g_pressed = 0; d_pressed = 0;
			return;
		}

		switch (c) {
		case '%': {
			if (pane->cy >= pane->numrows) break;
			erow *row = &pane->row[pane->cy];
			int start = pane->cx;
			int open = -1, close = -1, dir = 0;
			const char *pairs = "()[]{}";
			while (start < row->size) {
				const char *p = strchr(pairs, row->chars[start]);
				if (p) {
					int i = p - pairs;
					if (i % 2 == 0) { open = pairs[i]; close = pairs[i+1]; dir = 1; }
					else { open = pairs[i]; close = pairs[i-1]; dir = -1; }
					break;
				}
				start++;
			}
			if (dir == 0) { last_char = 0; g_pressed = 0; break; }
			hkPushJump(pane->cx, pane->cy);
			int depth = 1;
			int y = pane->cy, x = start;
			if (dir > 0) {
				x++;
				while (y < pane->numrows) {
					erow *r = &pane->row[y];
					while (x < r->size) {
						if (r->chars[x] == open) depth++;
						else if (r->chars[x] == close) {
							depth--;
							if (depth == 0) { pane->cy = y; pane->cx = x; goto pct_done; }
						}
						x++;
					}
					y++; x = 0;
				}
			} else {
				x--;
				while (y >= 0) {
					erow *r = &pane->row[y];
					while (x >= 0) {
						if (r->chars[x] == open) depth++;
						else if (r->chars[x] == close) {
							depth--;
							if (depth == 0) { pane->cy = y; pane->cx = x; goto pct_done; }
						}
						x--;
					}
					y--;
					if (y >= 0) x = pane->row[y].size - 1;
				}
			}
			editorSetStatusMessage("No matching bracket");
		pct_done:
			last_char = 0; g_pressed = 0;
			break;
		}

		case '*':
		case '#': {
			if (pane->cy >= pane->numrows) break;
			erow *row = &pane->row[pane->cy];
			int s = pane->cx, e = pane->cx;
			if (s >= row->size) break;
			if (is_separator(row->chars[s])) {
				while (s < row->size && is_separator(row->chars[s])) s++;
				if (s >= row->size) break;
				e = s;
			} else {
				while (s > 0 && !is_separator(row->chars[s - 1])) s--;
				e = s;
			}
			while (e < row->size && !is_separator(row->chars[e])) e++;
			int wlen = e - s;
			if (wlen <= 0) break;
			free(E.search.query);
			E.search.query = malloc(wlen + 1);
			memcpy(E.search.query, &row->chars[s], wlen);
			E.search.query[wlen] = '\0';
			hkPushJump(pane->cx, pane->cy);
			if (c == '*') editorFindNext(); else editorFindPrev();
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'm': {
			int mk = hkReadKeyBlocking();
			if (mk >= 'a' && mk <= 'z') {
				int i = mk - 'a';
				E.hk.marks_x[i] = pane->cx;
				E.hk.marks_y[i] = pane->cy;
				E.hk.marks_set |= (1 << i);
				editorSetStatusMessage("Mark '%c set", mk);
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case '`':
		case '\'': {
			int squote = (c == '\'');
			int mk = hkReadKeyBlocking();
			if (mk >= 'a' && mk <= 'z') {
				int i = mk - 'a';
				if (E.hk.marks_set & (1 << i)) {
					hkPushJump(pane->cx, pane->cy);
					pane->cy = E.hk.marks_y[i];
					pane->cx = squote ? 0 : E.hk.marks_x[i];
					if (squote && pane->cy < pane->numrows) {
						erow *r = &pane->row[pane->cy];
						while (pane->cx < r->size && isspace((unsigned char)r->chars[pane->cx])) pane->cx++;
					}
					hkClampCursor(pane);
				} else {
					editorSetStatusMessage("Mark '%c not set", mk);
				}
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case CTRL_KEY('o'):
			if (E.hk.jump_pos > 0) {
				if (E.hk.jump_pos == E.hk.jump_count) {
					hkPushJump(pane->cx, pane->cy);
					E.hk.jump_pos--;
				}
				E.hk.jump_pos--;
				pane->cx = E.hk.jumps[E.hk.jump_pos].x;
				pane->cy = E.hk.jumps[E.hk.jump_pos].y;
				hkClampCursor(pane);
			} else {
				editorSetStatusMessage("Jumplist empty");
			}
			last_char = 0; g_pressed = 0;
			break;

		case '\t':
			if (E.hk.jump_pos + 1 < E.hk.jump_count) {
				E.hk.jump_pos++;
				pane->cx = E.hk.jumps[E.hk.jump_pos].x;
				pane->cy = E.hk.jumps[E.hk.jump_pos].y;
				hkClampCursor(pane);
			}
			last_char = 0; g_pressed = 0;
			break;

		case 'i':
			hkRecordStart('i', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'I':
			pane->cx = 0;
			if (pane->cy < pane->numrows) {
				erow *row = &pane->row[pane->cy];
				while (pane->cx < row->size && isspace(row->chars[pane->cx]))
					pane->cx++;
			}
			hkRecordStart('I', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'a':
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				pane->cx = utf8_next_char(pane->row[pane->cy].chars, pane->cx, pane->row[pane->cy].size);
			}
			hkRecordStart('a', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'A':
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			hkRecordStart('A', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'o':
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			editorSaveState();
			editorInsertNewLine();
			hkRecordStart('o', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			break;

		case 'O':
			pane->cx = 0;
			editorSaveState();
			editorInsertRow(pane->cy, "", 0);
			hkRecordStart('O', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			break;

		case '.':
			hkDotRepeat();
			last_char = 0; g_pressed = 0;
			break;

		case 'x': {
			int n = hkConsumeCount();
			E.hk.last_op = 'x'; E.hk.last_count = n;
			editorSaveState();
			char *buf = NULL; int blen = 0; int bcap = 0;
			for (int k = 0; k < n; k++) {
				if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
					char ch = pane->row[pane->cy].chars[pane->cx];
					if (blen + 1 > bcap) { bcap = bcap ? bcap * 2 : 16; buf = realloc(buf, bcap); }
					buf[blen++] = ch;
					editorRowDelChar(&pane->row[pane->cy], pane->cx);
					if (pane->cx >= pane->row[pane->cy].size && pane->cx > 0) pane->cx = utf8_prev_char(pane->row[pane->cy].chars, pane->cx);
				}
			}
			if (blen > 0) hkSetRegister(E.hk.pending_reg, buf, blen, 0, 0);
			free(buf);
			E.hk.pending_reg = 0;
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'D':
			hkDeleteToEOL(pane);
			E.hk.pending_reg = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case 'C':
			hkDeleteToEOL(pane);
			E.hk.pending_reg = 0;
			editorSetMode(MODE_INSERT);
			break;

		case 'V':
			pane->visual_anchor_x = 0;
			pane->visual_anchor_y = pane->cy;
			editorSetMode(MODE_VISUAL_LINE);
			break;

		case 'v':
			pane->visual_anchor_x = pane->cx;
			pane->visual_anchor_y = pane->cy;
			editorSetMode(MODE_VISUAL);
			break;

		case 'u': {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorUndo();
			break;
		}

		case 'n': {
			int n = hkConsumeCount();
			if (E.search.query) {
				for (int k = 0; k < n; k++) editorFindNext();
			} else {
				editorSetStatusMessage("No previous search");
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'g':
			if (g_pressed) {
				int target = E.hk.pending_count > 0 ? E.hk.pending_count - 1 : 0;
				if (target < 0) target = 0;
				if (pane->numrows > 0 && target >= pane->numrows) target = pane->numrows - 1;
				hkPushJump(pane->cx, pane->cy);
				pane->cy = target;
				pane->cx = 0;
				E.hk.pending_count = 0;
				g_pressed = 0;
				if (d_pressed) {
					editorSaveState();
					while (pane->numrows > 0) {
						editorDelRow(0);
					}
					editorInsertRow(0, "", 0);
					pane->cx = pane->cy = 0;
					editorSetStatusMessage("Deleted all");
					d_pressed = 0;
				}
			} else {
				g_pressed = 1;
			}
			last_char = 0;
			break;

		case 'G':
			if (d_pressed) {
				editorSaveState();
				while (pane->cy < pane->numrows - 1) {
					editorDelRow(pane->cy + 1);
				}
				if (pane->cy < pane->numrows) {
					pane->row[pane->cy].size = pane->cx;
					pane->row[pane->cy].chars[pane->cx] = '\0';
					editorUpdateRow(&pane->row[pane->cy]);
				}
				editorSetStatusMessage("Deleted to end of file");
				d_pressed = 0;
				E.hk.pending_count = 0;
			} else {
				hkPushJump(pane->cx, pane->cy);
				if (E.hk.pending_count > 0) {
					int target = E.hk.pending_count - 1;
					if (pane->numrows > 0 && target >= pane->numrows) target = pane->numrows - 1;
					if (target < 0) target = 0;
					pane->cy = target;
					pane->cx = 0;
				} else {
					pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
				}
				E.hk.pending_count = 0;
			}
			g_pressed = 0;
			last_char = 0;
			break;

		case 'y':
			if (last_char == 'y') {
				int n = hkConsumeCount();
				if (pane->cy < pane->numrows) {
					int lines = MIN(n, pane->numrows - pane->cy);
					hkYankLinesToReg(pane, n, 1);
					editorSetStatusMessage("%d line%s yanked", lines, lines == 1 ? "" : "s");
				}
				E.hk.pending_reg = 0;
				last_char = 0;
			} else {
				last_char = 'y';
				if (E.hk.pending_reg) editorSetStatusMessage("\"%c y", E.hk.pending_reg);
				else editorSetStatusMessage("y");
			}
			g_pressed = 0;
			break;

		case 'r':
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				int next_char = hkReadKeyBlocking();
				if (next_char != '\x1b' && !iscntrl(next_char)) {
					editorSaveState();
					editorRowDelChar(&pane->row[pane->cy], pane->cx);
					editorRowInsertChar(&pane->row[pane->cy], pane->cx, next_char);
				}
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case 'J': {
			int n = hkConsumeCount();
			E.hk.last_op = 'J'; E.hk.last_count = n;
			editorSaveState();
			hkJoinLines(pane, n);
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'w':
			if (d_pressed || last_char == 'c') {
				int was_change = (last_char == 'c');
				editorSaveState();
				if (pane->cy < pane->numrows) {
					hkDeleteWordForward(pane);
					if (was_change) {
						hkRecordStart('W', 1);
						editorSetMode(MODE_INSERT);
					} else {
						E.hk.last_op = 'w';
						E.hk.last_count = 1;
					}
				}
				E.hk.pending_reg = 0;
				last_char = 0;
				d_pressed = 0;
				g_pressed = 0;
				break;
			}
			{
				int n = hkConsumeCount();
				for (int k = 0; k < n; k++) hkWordForward(pane);
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case 'b': {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) hkWordBack(pane);
			last_char = 0;
			g_pressed = 0;
			break;
		}

		case ']':
		case '[': {
			int k2 = hkReadKeyBlocking();
			if (k2 == 'e') runnerErrorStep(c == ']' ? 1 : -1);
			last_char = 0; g_pressed = 0;
			break;
		}

		case '0':
		case HOME_KEY:
			pane->cx = 0;
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case '$':
		case END_KEY:
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case 'N': {
			int n = hkConsumeCount();
			if (E.search.query) {
				for (int k = 0; k < n; k++) editorFindPrev();
			} else {
				editorSetStatusMessage("No previous search");
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case CTRL_KEY('r'): {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorRedo();
			break;
		}

		case CTRL_KEY('c'):
			editorCopyToSystemClipboard();
			break;

		case CTRL_KEY('v'):
			editorPasteFromSystemClipboard();
			break;

		case 'p': {
			int n = hkConsumeCount();
			pasteBuffer *pb = hkGetRegister(E.hk.pending_reg);
			E.hk.pending_reg = 0;
			if (pb && pb->data && pb->len > 0) {
				editorSaveState();
				for (int k = 0; k < n; k++) {
					if (pb->is_line_mode) {
						pane->cx = (pane->cy < pane->numrows) ? pane->row[pane->cy].size : 0;
						editorInsertNewLine();
						editorHandlePaste(pb->data, pb->len);
					} else {
						if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size)
							pane->cx = utf8_next_char(pane->row[pane->cy].chars, pane->cx, pane->row[pane->cy].size);
						editorHandlePaste(pb->data, pb->len);
					}
				}
			} else {
				editorSetStatusMessage("Nothing to paste");
			}
			break;
		}

		case 'P': {
			int n = hkConsumeCount();
			pasteBuffer *pb = hkGetRegister(E.hk.pending_reg);
			E.hk.pending_reg = 0;
			if (pb && pb->data && pb->len > 0) {
				editorSaveState();
				for (int k = 0; k < n; k++) {
					if (pb->is_line_mode) {
						pane->cx = 0;
						editorInsertNewLine();
						pane->cy--;
						pane->cx = 0;
						editorHandlePaste(pb->data, pb->len);
					} else {
						editorHandlePaste(pb->data, pb->len);
					}
				}
			} else {
				editorSetStatusMessage("Nothing to paste");
			}
			break;
		}

		case ':':
			editorColonCommand();
			break;

		case '/':
			editorFind();
			break;

		case '\x1b':
			if (E.search.query) {
				free(E.search.query);
				E.search.query = NULL;
				hkRefreshSyntax(pane);
				editorSetStatusMessage("Search cleared");
			}
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			d_pressed = 0;
			break;

		case 'h':
		case 'j':
		case 'k':
		case 'l':
		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN: {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorMoveCursor(c);
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'd':
			if (last_char == 'd') {
				int n = hkConsumeCount();
				E.hk.last_op = 'd'; E.hk.last_count = n;
				editorSaveState();

				if (pane->cy < pane->numrows) {
					int to_del = MIN(n, pane->numrows - pane->cy);
					hkYankLinesToReg(pane, n, 0);
					for (int k = 0; k < to_del; k++) editorDelRow(pane->cy);
				}
				E.hk.pending_reg = 0;

				if (pane->numrows == 0) {
					editorInsertRow(0, "", 0);
					pane->rowoff = pane->coloff = 0;
				}

				if (pane->cy >= pane->numrows) pane->cy = pane->numrows - 1;
				if (pane->cy < 0) pane->cy = 0;

				pane->cx = 0;

				editorSetStatusMessage("%d line%s deleted", n, n == 1 ? "" : "s");

				last_char = 0;
				d_pressed = 0;
			} else {
				last_char = 'd';
				d_pressed = 1;
			}
			g_pressed = 0;
			break;

		case 'c':
			if (last_char == 'c') {
				editorSaveState();
				if (pane->cy < pane->numrows) {
					erow *row = &pane->row[pane->cy];
					hkSetRegister(E.hk.pending_reg, row->chars, row->size, 0, 0);
					row->size = 0;
					row->chars[0] = '\0';
					editorUpdateRow(row);
					pane->cx = 0;
					pane->dirty++;
				}
				E.hk.pending_reg = 0;
				editorSetMode(MODE_INSERT);
				last_char = 0;
				d_pressed = 0;
			} else {
				last_char = 'c';
				d_pressed = 0;
			}
			g_pressed = 0;
			break;

		case CTRL_KEY('f'):
		case PAGE_DOWN:
			if (E.word_wrap && pane->wrap_lines) {
				int wrap_width = hkWrapWidth(pane);
				int visual_rows = hkVisualRowOfLine(pane, pane->numrows, wrap_width);
				int old_rowoff = pane->rowoff;
				pane->rowoff = MIN(pane->rowoff + (pane->height - hkPaneHdr(pane)), MAX(0, visual_rows - (pane->height - hkPaneHdr(pane))));
				if (pane->rowoff != old_rowoff) {
					int r = hkRowAtVisual(pane, pane->rowoff, wrap_width, NULL);
					if (r >= 0) {
						pane->cy = r;
						pane->cx = 0;
					}
				}
			} else {
				pane->rowoff = MIN(pane->rowoff + (pane->height - hkPaneHdr(pane)), MAX(0, pane->numrows - (pane->height - hkPaneHdr(pane))));
				pane->cy = MIN(pane->rowoff + (pane->height - hkPaneHdr(pane)) - 1, pane->numrows - 1);
				if (pane->cy < pane->rowoff) pane->cy = pane->rowoff;
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case CTRL_KEY('b'):
		case PAGE_UP:
			if (E.word_wrap && pane->wrap_lines) {
				int old_rowoff = pane->rowoff;
				pane->rowoff = MAX(0, pane->rowoff - (pane->height - hkPaneHdr(pane)));
				if (pane->rowoff != old_rowoff) {
					int wrap_width = hkWrapWidth(pane);
					int r = hkRowAtVisual(pane, pane->rowoff + (pane->height - hkPaneHdr(pane)) - 1, wrap_width, NULL);
					if (r >= 0) {
						pane->cy = r;
						pane->cx = 0;
					}
				}
			} else {
				pane->rowoff = MAX(0, pane->rowoff - (pane->height - hkPaneHdr(pane)));
				pane->cy = pane->rowoff;
			}
			last_char = 0;
			g_pressed = 0;
			break;

		default:
			last_char = 0;
			g_pressed = 0;
			d_pressed = 0;
			E.hk.pending_count = 0;
			break;
		}
	} else if (E.mode == MODE_INSERT) {
		int should_save = 0;
		time_t now = time(NULL);
		
		if (now - last_undo_time > 2) should_save = 1;

		if (!E.is_pasting && editorInputPending()) {
			E.is_pasting = 1;
			editorSaveState();
			last_undo_time = now;
			should_save = 0;
		}

		if (E.is_pasting && !editorInputPending()) {
			E.is_pasting = 0;
		}

		switch (c) {
		case '\r':
			if (last_action != c) should_save = 1;
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			editorInsertNewLine();
			hkRecordChar('\n');
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (last_action != c) should_save = 1;
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			hkRecordBackspace();
			break;

		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;

		case CTRL_KEY('q'):
			editorSetStatusMessage("Use :q in normal mode to quit.");
			break;

		case '\x1b':
			editorSaveState();
			hkRecordStop();
			editorSetMode(MODE_NORMAL);
			break;

		default:
			if (!isprint(c) || (last_action != 'i' && isprint(last_action))) {
				should_save = 1;
			}
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			editorInsertChar(c);
			hkRecordChar(c);
			break;
		}
		
		last_action = c;
	} else if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
		switch (c) {
		case '\x1b':
			editorSetMode(MODE_NORMAL);
			break;

		case 'j':
		case 'k':
		case ARROW_UP:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;

		case 'h':
		case ARROW_LEFT:
			if (E.mode == MODE_VISUAL) {
				editorMoveCursor(c);
			}
			break;

		case 'l':
		case ARROW_RIGHT:
			if (E.mode == MODE_VISUAL) {
				editorMoveCursor(c);
			}
			break;

		case 'w':
			hkWordForward(pane);
			break;

		case 'b':
			hkWordBack(pane);
			break;

		case 'G':
			pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
			if (pane->cy < pane->numrows)
				pane->cx = pane->row[pane->cy].size;
			break;

		case 'g':
			pane->cy = 0;
			pane->cx = 0;
			break;

		case '0':
			pane->cx = 0;
			break;

		case '$':
			if (pane->cy < pane->numrows)
				pane->cx = pane->row[pane->cy].size;
			break;

		case CTRL_KEY('f'):
		case PAGE_DOWN:
			{
				int times = pane->height - hkPaneHdr(pane);
				while (times-- && pane->cy < pane->numrows - 1) {
					pane->cy++;
				}
			}
			break;

		case CTRL_KEY('b'):
		case PAGE_UP:
			{
				int times = pane->height - hkPaneHdr(pane);
				while (times-- && pane->cy > 0) {
					pane->cy--;
				}
			}
			break;

		case 'd':
		case 'x':
			editorDeleteVisualSelection();
			editorSetMode(MODE_NORMAL);
			break;

		case 'y':
			editorYankVisualSelection();
			editorSetMode(MODE_NORMAL);
			break;

		case 'c':
			editorDeleteVisualSelection();
			editorSetMode(MODE_INSERT);
			break;
			
		case CTRL_KEY('c'):
			editorYankVisualSelection();
			editorCopyToSystemClipboard();
			editorSetMode(MODE_NORMAL);
			break;
		}
	}
}

/*** scrollable popup overlay ***/

static int hkThemeNames(const char **out, int max);

void editorPopupFree(void) {
	if (E.popup_lines) {
		for (int i = 0; i < E.popup_count; i++) free(E.popup_lines[i]);
		free(E.popup_lines);
		E.popup_lines = NULL;
	}
	E.popup_count = 0;
}

void editorPopupOpen(const char *title, const char *const *items, int count,
                     int selectable, int preview) {
	editorPopupFree();
	if (count < 0) count = 0;
	E.popup_lines = count ? calloc(count, sizeof(char *)) : NULL;
	if (count && !E.popup_lines) return;
	for (int i = 0; i < count; i++) {
		E.popup_lines[i] = strdup(items[i] ? items[i] : "");
		if (!E.popup_lines[i]) { E.popup_count = i; editorPopupFree(); return; }
	}
	E.popup_count = count;
	E.popup_scroll = 0;
	E.popup_sel = 0;
	E.popup_selectable = selectable;
	E.popup_preview = preview;
	E.popup_apply = NULL;
	E.popup_choice_id = 0;
	snprintf(E.popup_title, sizeof(E.popup_title), "%s", title ? title : "");
	snprintf(E.popup_saved_theme, sizeof(E.popup_saved_theme), "%s", E.theme_name);
	E.popup_active = 1;
}

void editorPopupOpenHelp(void) {
	const char *mode = E.keymode == HK_KEYS_EMACS ? HK_EMACS_HELP : E.keymode == HK_KEYS_NANO ? HK_NANO_HELP : "";
	char *txt = malloc(strlen(mode) + strlen(HAKE_HELP_TEXT) + 1);
	if (txt) { strcpy(txt, mode); strcat(txt, HAKE_HELP_TEXT); }
	if (!txt) return;
	int cap = 64, n = 0;
	char **lines = malloc(cap * sizeof(char *));
	if (!lines) { free(txt); return; }
	char *p = txt;
	while (1) {
		char *nl = strchr(p, '\n');
		if (nl) *nl = '\0';
		if (n >= cap) {
			cap *= 2;
			char **nb = realloc(lines, cap * sizeof(char *));
			if (!nb) break;
			lines = nb;
		}
		lines[n++] = p;
		if (!nl) break;
		p = nl + 1;
	}
	editorPopupOpen("Help: commands & keys", (const char *const *)lines, n, 0, 0);
	free(lines);
	free(txt);
}

void editorPopupOpenThemes(void) {
	const char *names[96];
	int n = hkThemeNames(names, 96);
	editorPopupOpen("Theme", names, n, 1, 1);
	for (int i = 0; i < n; i++) {
		if (strcmp(names[i], E.theme_name) == 0) { E.popup_sel = i; break; }
	}
}

void editorPopupClose(int apply) {
	if (!E.popup_active) return;
	if (E.popup_choice_id) {
		/* Rei picker: hako is blocked waiting for the answer. A 1-based index
		   on apply, an empty answer on cancel (Esc) so it does not hang. */
		char msg[96];
		if (apply && E.popup_count > 0)
			snprintf(msg, sizeof(msg), "{\"type\":\"choice\",\"id\":%ld,\"answer\":\"%d\"}",
			         E.popup_choice_id, E.popup_sel + 1);
		else
			snprintf(msg, sizeof(msg), "{\"type\":\"choice\",\"id\":%ld,\"answer\":\"\"}",
			         E.popup_choice_id);
		hakoSendLine(msg);
		E.popup_choice_id = 0;
		E.popup_active = 0;
		editorPopupFree();
			return;
	}
	if (E.popup_preview && !apply) {
		if (E.popup_saved_theme[0] &&
		    strcmp(E.popup_saved_theme, E.theme_name) != 0) {
			editorApplyThemeByName(E.popup_saved_theme);
		}
	} else if (apply && E.popup_selectable && E.popup_count > 0) {
		const char *sel = E.popup_lines[E.popup_sel];
		if (E.popup_preview) hkSaveTheme(sel);
		else if (E.popup_apply) {
			void (*fn)(int) = E.popup_apply;
			int pick = E.popup_sel;
			E.popup_active = 0;
			editorPopupFree();
			fn(pick);
			return;
		}
	}
	E.popup_active = 0;
	editorPopupFree();
}

static int hkPopupBodyRows(void) {
	int rows = E.screenrows - 8;
	if (rows > E.popup_count) rows = E.popup_count;
	if (rows < 1) rows = 1;
	if (rows > 24) rows = 24;
	return rows;
}

static void hkPopupGeom(int *x0, int *y0, int *boxw, int *boxh, int *inner, int *body) {
	int b = hkPopupBodyRows();
	int maxw = (int)strlen(E.popup_title);
	for (int i = 0; i < E.popup_count; i++) {
		int l = (int)strlen(E.popup_lines[i]);
		if (l > maxw) maxw = l;
	}
	int in = maxw + 2;
	int max_in = E.screencols - 4;
	if (in > max_in) in = max_in;
	if (in < 12) in = 12;
	int bw = in + 2, bh = b + 4;
	int X = (E.screencols - bw) / 2; if (X < 0) X = 0;
	int Y = (E.screenrows - bh) / 2; if (Y < 1) Y = 1;
	*x0 = X; *y0 = Y; *boxw = bw; *boxh = bh; *inner = in; *body = b;
}

static void hkPopupClampScroll(void) {
	int body = hkPopupBodyRows();
	if (E.popup_selectable) {
		if (E.popup_sel < 0) E.popup_sel = 0;
		if (E.popup_sel >= E.popup_count) E.popup_sel = E.popup_count - 1;
		if (E.popup_sel < E.popup_scroll) E.popup_scroll = E.popup_sel;
		if (E.popup_sel >= E.popup_scroll + body) E.popup_scroll = E.popup_sel - body + 1;
	}
	int max_scroll = E.popup_count - body;
	if (max_scroll < 0) max_scroll = 0;
	if (E.popup_scroll > max_scroll) E.popup_scroll = max_scroll;
	if (E.popup_scroll < 0) E.popup_scroll = 0;
}

void editorPopupHandleKey(int c) {
	static int g_pending = 0;
	int body = hkPopupBodyRows();

	if (c == MOUSE_MOTION || c == 0) return;

	if (c == MOUSE_CLICK) {
		int x0, y0, boxw, boxh, inner, gbody;
		hkPopupGeom(&x0, &y0, &boxw, &boxh, &inner, &gbody);
		int mx = E.mouse_x, my = E.mouse_y;
		if (mx < x0 || mx >= x0 + boxw || my < y0 || my >= y0 + boxh) {
			editorPopupClose(0);
			return;
		}
		int r = my - (y0 + 3);
		if (r >= 0 && r < gbody && E.popup_selectable) {
			int idx = E.popup_scroll + r;
			if (idx >= 0 && idx < E.popup_count) {
				if (idx == E.popup_sel) { editorPopupClose(1); return; }
				E.popup_sel = idx;
				hkPopupClampScroll();
				if (E.popup_preview && strcmp(E.popup_lines[E.popup_sel], E.theme_name) != 0)
					editorApplyThemeByName(E.popup_lines[E.popup_sel]);
						}
		}
		return;
	}

	if (c == 'g') {
		if (g_pending) { g_pending = 0;
			if (E.popup_selectable) E.popup_sel = 0; else E.popup_scroll = 0; }
		else g_pending = 1;
	} else {
		g_pending = 0;
		switch (c) {
		case '\x1b':
		case 'q':
			editorPopupClose(0);
			return;
		case '\r':
		case '\n':
			editorPopupClose(1);
			return;
		case 'j':
		case ARROW_DOWN:
		case MOUSE_WHEEL_DOWN:
			if (E.popup_selectable) E.popup_sel++; else E.popup_scroll++;
			break;
		case 'k':
		case ARROW_UP:
		case MOUSE_WHEEL_UP:
			if (E.popup_selectable) E.popup_sel--; else E.popup_scroll--;
			break;
		case CTRL_KEY('d'):
		case CTRL_KEY('f'):
		case PAGE_DOWN:
			if (E.popup_selectable) E.popup_sel += body; else E.popup_scroll += body;
			break;
		case CTRL_KEY('u'):
		case CTRL_KEY('b'):
		case PAGE_UP:
			if (E.popup_selectable) E.popup_sel -= body; else E.popup_scroll -= body;
			break;
		case 'G':
			if (E.popup_selectable) E.popup_sel = E.popup_count - 1;
			else E.popup_scroll = E.popup_count;
			break;
		default:
			break;
		}
	}

	hkPopupClampScroll();
	if (E.popup_preview && E.popup_selectable && E.popup_count > 0) {
		if (strcmp(E.popup_lines[E.popup_sel], E.theme_name) != 0)
			editorApplyThemeByName(E.popup_lines[E.popup_sel]);
	}
}

void editorDrawPopup() {
	if (!E.popup_active || E.popup_count <= 0) return;
	hkPopupClampScroll();

	int x0, y0, boxw, boxh, inner, body;
	hkPopupGeom(&x0, &y0, &boxw, &boxh, &inner, &body);

	Color bd = E.theme.border, bg = E.theme.bg, fg = E.theme.fg;
	Color tfg = E.theme.fg, dim = E.theme.line_number;
	Color sel_fg = E.theme.visual_fg, sel_bg = E.theme.visual_bg;

	char line[512];
	gridPutStr(x0, y0, "\xe2\x95\xad", 1, bd, bg);
	for (int x = 1; x < boxw - 1; x++) gridPutStr(x0 + x, y0, "\xe2\x94\x80", 1, bd, bg);
	gridPutStr(x0 + boxw - 1, y0, "\xe2\x95\xae", 1, bd, bg);

	gridPutStr(x0, y0 + 1, "\xe2\x94\x82", 1, bd, bg);
	for (int x = 1; x < boxw - 1; x++) gridPutCh(x0 + x, y0 + 1, ' ', tfg, bg);
	snprintf(line, sizeof(line), " %s", E.popup_title);
	gridPutStr(x0 + 1, y0 + 1, line, inner, tfg, bg);
	gridPutStr(x0 + boxw - 1, y0 + 1, "\xe2\x94\x82", 1, bd, bg);

	gridPutStr(x0, y0 + 2, "\xe2\x94\x9c", 1, bd, bg);
	for (int x = 1; x < boxw - 1; x++) gridPutStr(x0 + x, y0 + 2, "\xe2\x94\x80", 1, bd, bg);
	gridPutStr(x0 + boxw - 1, y0 + 2, "\xe2\x94\xa4", 1, bd, bg);

	for (int r = 0; r < body; r++) {
		int gy = y0 + 3 + r;
		int idx = E.popup_scroll + r;
		gridPutStr(x0, gy, "\xe2\x94\x82", 1, bd, bg);
		gridPutStr(x0 + boxw - 1, gy, "\xe2\x94\x82", 1, bd, bg);
		int selected = (E.popup_selectable && idx == E.popup_sel);
		Color rfg = selected ? sel_fg : fg;
		Color rbg = selected ? sel_bg : bg;
		for (int x = 1; x < boxw - 1; x++) gridPutCh(x0 + x, gy, ' ', rfg, rbg);
		if (idx < E.popup_count) {
			const char *prefix = selected ? "\xe2\x96\xb8 " : "  ";
			snprintf(line, sizeof(line), "%s%s",
			         E.popup_selectable ? prefix : "", E.popup_lines[idx]);
			gridPutStr(x0 + 1, gy, line, inner, rfg, rbg);
		}
	}

	gridPutStr(x0, y0 + boxh - 1, "\xe2\x95\xb0", 1, bd, bg);
	for (int x = 1; x < boxw - 1; x++) gridPutStr(x0 + x, y0 + boxh - 1, "\xe2\x94\x80", 1, bd, bg);
	gridPutStr(x0 + boxw - 1, y0 + boxh - 1, "\xe2\x95\xaf", 1, bd, bg);
	const char *hint = E.popup_selectable ? " j/k move  enter apply  esc cancel "
	                                       : " j/k scroll  esc close ";
	int hlen = (int)strlen(hint);
	if (hlen < boxw - 2) gridPutStr(x0 + boxw - 1 - hlen, y0 + boxh - 1, hint, hlen, dim, bg);
	if (E.popup_count > body) {
		char pos[32];      /* two ints plus " / " never exceeds 26 */
		snprintf(pos, sizeof(pos), " %d/%d ",
		         (E.popup_selectable ? E.popup_sel : E.popup_scroll) + 1, E.popup_count);
		gridPutStr(x0 + 2, y0 + boxh - 1, pos, (int)strlen(pos), dim, bg);
	}
}

/*** explorer ***/
int explorerCompare(const void *a, const void *b) {
	const char *sa = *(const char **)a;
	const char *sb = *(const char **)b;
	int a_dir = (strchr(sa, '/') != NULL);
	int b_dir = (strchr(sb, '/') != NULL);
	if (strcmp(sa, "../") == 0) return -1;
	if (strcmp(sb, "../") == 0) return 1;
	if (a_dir != b_dir) return b_dir - a_dir;
	return strcasecmp(sa, sb);
}

void explorerInit(editorPane *pane) {
	explorerData *data = calloc(1, sizeof(explorerData));
	if (!data) return;

	data->current_dir = getcwd(NULL, 0);
	pane->explorer = data;
	
	explorerRefresh(pane);
}

void explorerRefresh(editorPane *pane) {
	explorerData *data = pane->explorer;
	if (!data) return;

	for (int i = 0; i < data->num_entries; i++) {
		free(data->entries[i]);
	}
	free(data->entries);
	data->entries = NULL;
	data->num_entries = 0;

	DIR *dir = opendir(data->current_dir);
	if (!dir) return;

	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (!E.explorer_show_hidden && ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
		if (strcmp(ent->d_name, ".") == 0) continue;
		data->num_entries++;
	}

	if (data->num_entries == 0) {
		closedir(dir);
		return;
	}

	data->entries = malloc(sizeof(char*) * data->num_entries);
	rewinddir(dir);

	int i = 0;
	while ((ent = readdir(dir)) && i < data->num_entries) {
		if (!E.explorer_show_hidden && ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
		if (strcmp(ent->d_name, ".") == 0) continue;

		char full_path[PATH_MAX];
		snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, ent->d_name);

		struct stat st;
		int is_dir = 0;
		if (stat(full_path, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode);
		}

		size_t bufsz = strlen(ent->d_name) + 4;
		data->entries[i] = malloc(bufsz);
		if (is_dir) {
			snprintf(data->entries[i], bufsz, "%s/", ent->d_name);
		} else {
			snprintf(data->entries[i], bufsz, "%s", ent->d_name);
		}
		i++;
	}

	closedir(dir);

	qsort(data->entries, data->num_entries, sizeof(char*), explorerCompare);

	if (data->selected >= data->num_entries) {
		data->selected = data->num_entries > 0 ? data->num_entries - 1 : 0;
	}
	
	if (data->selected < data->scroll_offset) {
		data->scroll_offset = data->selected;
	}
	if (data->selected >= data->scroll_offset + pane->height - 2) {
		data->scroll_offset = data->selected - pane->height + 3;
	}
}

void explorerRender(editorPane *pane, struct abuf *ab) {
	(void)ab;
	explorerData *data = pane->explorer;
	if (!data) return;

	for (int y = 0; y < pane->height; y++) {
		int gy = pane->y + y;
		int gx = pane->x;

		if (y == 0) {
			Color hf = E.theme.status_fg;
			Color hb = E.theme.status_bg;
			char header[64];
			snprintf(header, sizeof(header), " %s ", E.explorer_name);
			int used = gridPutStr(gx, gy, header, pane->width, hf, hb);
			for (int x = used; x < pane->width; x++) {
				gridPutCh(gx + x, gy, ' ', hf, hb);
			}
		} else if (y == 1) {
			int used = 0;
			while (used < pane->width) {
				int w = gridPutStr(gx + used, gy, "─", pane->width - used, E.theme.border, E.theme.bg);
				if (w <= 0) break;
				used += w;
			}
		} else {
			int idx = (y - 2) + data->scroll_offset;
			if (idx < data->num_entries && data->entries && data->entries[idx]) {
				Color row_fg, row_bg;
				if (idx == data->selected) {
					row_fg = E.theme.visual_fg;
					row_bg = E.theme.visual_bg;
				} else {
					int is_dir = (strchr(data->entries[idx], '/') != NULL);
					row_fg = is_dir ? E.theme.keyword2 : E.theme.fg;
					row_bg = E.theme.bg;
				}
				gridPutCh(gx, gy, ' ', row_fg, row_bg);
				int name_len = strlen(data->entries[idx]);
				int max = pane->width - 2;
				if (name_len > max) name_len = max;
				int used = gridPutStr(gx + 1, gy, data->entries[idx], max, row_fg, row_bg);
				for (int x = 1 + used; x < pane->width; x++) {
					gridPutCh(gx + x, gy, ' ', row_fg, row_bg);
				}
			} else {
				for (int x = 0; x < pane->width; x++) {
					gridPutCh(gx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
			}
		}
	}
}

static int explorerOpenInSplit(const char *full_path) {
	editorPane *target = hkFirstEditorLeaf();
	if (!target) {
		editorSetStatusMessage("No editor pane available");
		return 0;
	}
	if (target->width < 40) return -1;

	int prev_num_panes = E.num_panes;
	E.active_pane = target;
	editorSplitPane(1);
	if (E.num_panes == prev_num_panes) return 0;

	editorPane *np = E.active_pane;
	hkResetPaneBuffer(np);
	free(np->filename);
	np->filename = NULL;
	np->dirty = 0;

	editorOpen((char *)full_path);
	editorSetStatusMessage("Opened in new pane: %s", full_path);
	return 1;
}

void explorerHandleKey(editorPane *pane, int key) {
	explorerData *data = pane->explorer;
	if (!data) return;

	switch (key) {
	case 'j':
	case ARROW_DOWN:
		if (data->selected < data->num_entries - 1) {
			data->selected++;
			if (data->selected >= data->scroll_offset + pane->height - 2) {
				data->scroll_offset++;
			}
		}
		break;

	case 'k':
	case ARROW_UP:
		if (data->selected > 0) {
			data->selected--;
			if (data->selected < data->scroll_offset) {
				data->scroll_offset--;
			}
		}
		break;

	case 'g':
		data->selected = 0;
		data->scroll_offset = 0;
		break;

	case 'G':
		if (data->num_entries > 0) {
			data->selected = data->num_entries - 1;
			data->scroll_offset = MAX(0, data->num_entries - pane->height + 2);
		}
		break;

	case ':':
		editorColonCommand();
		break;

	case '\r':
	case 'l':
	case ARROW_RIGHT:
		if (data->num_entries > 0 && data->entries && data->entries[data->selected]) {
			char full_path[PATH_MAX];
			snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, data->entries[data->selected]);
			
			if (strchr(data->entries[data->selected], '/')) {
#ifdef _WIN32
				char rd[PATH_MAX];
				char *new_dir = _fullpath(rd, full_path, PATH_MAX) ? strdup(rd) : NULL;
#else
				char *new_dir = realpath(full_path, NULL);
#endif
				if (new_dir) {
					free(data->current_dir);
					data->current_dir = new_dir;
					data->selected = 0;
					data->scroll_offset = 0;
					explorerRefresh(pane);
				}
			} else {
				editorPane *target = hkFirstEditorLeaf();
				if (target && target->dirty) {
					if (explorerOpenInSplit(full_path) == -1)
						editorSetStatusMessage("Unsaved changes and no room to split — :w first or force with 'o'");
				} else if (target) {
					E.active_pane = target;
					hkResetPaneBuffer(target);
					editorOpen(full_path);
					editorSetStatusMessage("Opened: %s", full_path);
				}
			}
		}
		break;

	case 'h':
	case ARROW_LEFT:
		if (strcmp(data->current_dir, "/") != 0) {
			char *parent = strdup(data->current_dir);
			char *last_slash = strrchr(parent, '/');
			if (last_slash && last_slash != parent) {
				*last_slash = '\0';
			} else {
				strcpy(parent, "/");
			}
			free(data->current_dir);
			data->current_dir = parent;
			data->selected = 0;
			data->scroll_offset = 0;
			explorerRefresh(pane);
		}
		break;

	case 'o':
		if (data->num_entries > 0 && data->entries && data->entries[data->selected] &&
			!strchr(data->entries[data->selected], '/')) {
			char full_path[PATH_MAX];
			snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, data->entries[data->selected]);

			if (explorerOpenInSplit(full_path) == -1)
				editorSetStatusMessage("Not enough room to split — use <Enter> to open in current pane");
		}
		break;

	case '.':
		E.explorer_show_hidden = !E.explorer_show_hidden;
		explorerRefresh(pane);
		editorSetStatusMessage("Hidden files: %s", E.explorer_show_hidden ? "shown" : "hidden");
		break;

	case 'r':
		explorerRefresh(pane);
		editorSetStatusMessage("Explorer refreshed");
		break;

	case 'q':
	case '\x1b':
		editorToggleExplorer();
		break;
	}
}

void explorerCleanup(editorPane *pane) {
	explorerData *data = pane->explorer;
	if (!data) return;

	free(data->current_dir);
	for (int i = 0; i < data->num_entries; i++) {
		free(data->entries[i]);
	}
	free(data->entries);
	free(data);
	pane->explorer = NULL;
}

static void hkCloseSidePanel(editorPane **panel, editorPane *fallback) {
	if (E.active_pane == *panel) {
		editorPane **leaves = NULL;
		int lcount = 0;
		editorCollectLeafPanes(E.root_pane, &leaves, &lcount);
		if (lcount > 0) {
			E.active_pane = leaves[0];
			E.active_pane->is_focused = 1;
		} else if (fallback) {
			E.active_pane = fallback;
			E.active_pane->is_focused = 1;
		} else {
			E.active_pane = NULL;
		}
		free(leaves);
	}
	editorFreePane(*panel);
	*panel = NULL;
	editorResizePanes();
}

void editorToggleExplorer() {
	if (E.left_panel && E.left_panel->type == PANE_EXPLORER) {
		hkCloseSidePanel(&E.left_panel, E.right_panel);
		return;
	}
	
	if (E.left_panel && E.left_panel->type == PANE_AI) {
		editorSetStatusMessage("AI panel is on the left. Close it first or move it");
		return;
	}
	
	E.left_panel_width = E.explorer_width > 0 ? E.explorer_width : 30;
	E.left_panel = editorCreatePane(PANE_EXPLORER, 0, 0, E.left_panel_width, E.screenrows);
	E.left_panel->wrap_lines = 0;
	explorerInit(E.left_panel);

	editorResizePanes();
}

/*** runner ***/
#define RUNNER_MAX_LINES 5000

static int runnerParseError(const char *line, char *path, size_t psz, int *lno, int *col) {
	const char *c = strchr(line, ':');
	while (c) {
		if (isdigit((unsigned char)c[1])) {
			int n = (int)(c - line);
			if (n > 0 && n < (int)psz) {
				memcpy(path, line, n);
				path[n] = '\0';
				if (access(path, F_OK) == 0) {
					int l = atoi(c + 1);
					const char *c2 = strchr(c + 1, ':');
					int cl = (c2 && isdigit((unsigned char)c2[1])) ? atoi(c2 + 1) : 0;
					if (l > 0) {
						*lno = l;
						*col = cl;
						return 1;
					}
				}
			}
		}
		c = strchr(c + 1, ':');
	}
	return 0;
}

static void runnerJumpTo(const char *path, int lno, int col) {
	editorPane *target = hkFirstEditorLeaf();
	if (!target) return;
	char resolved[PATH_MAX];
	const char *open_path = realpath(path, resolved) ? resolved : path;
	if (!target->filename || strcmp(target->filename, open_path) != 0) {
		if (target->dirty) {
			if (explorerOpenInSplit(open_path) != 1) {
				editorSetStatusMessage("Unsaved changes and no room to split — :w first");
				return;
			}
			target = E.active_pane;
		} else {
			E.active_pane = target;
			hkResetPaneBuffer(target);
			editorOpen((char *)open_path);
		}
	}
	E.active_pane = target;
	target->is_focused = 1;
	hkPushJump(target->cx, target->cy);
	target->cy = lno - 1;
	target->cx = col > 0 ? col - 1 : 0;
	hkClampCursor(target);
	target->rowoff = target->cy > 5 ? target->cy - 5 : 0;
	target->coloff = 0;
}

static void runnerAppend(runnerData *r, const char *line) {
	pthread_mutex_lock(&r->lock);
	if (r->count >= RUNNER_MAX_LINES) {
		if (!r->truncated) {
			r->truncated = 1;
			if (r->count < r->cap) r->lines[r->count++] = strdup("… output truncated");
		}
		pthread_mutex_unlock(&r->lock);
		return;
	}
	if (r->count >= r->cap) {
		r->cap = r->cap ? r->cap * 2 : 128;
		r->lines = realloc(r->lines, r->cap * sizeof(char *));
	}
	int follow = (r->count == 0 || r->selected == r->count - 1);
	r->lines[r->count++] = strdup(line);
	if (follow) r->selected = r->count - 1;
	pthread_mutex_unlock(&r->lock);
}

#ifndef _WIN32
static void *runnerThread(void *arg) {
	runnerData *r = (runnerData *)arg;
	char line[4096];
	while (r->fp && fgets(line, sizeof(line), r->fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		runnerAppend(r, line);       /* the next 100 ms tick repaints it; a full clear per line flickered */
	}
	int st = 0;
	waitpid(r->pid, &st, 0);
	pthread_mutex_lock(&r->lock);
	r->running = 0;
	r->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	pthread_mutex_unlock(&r->lock);
	return NULL;
}
#endif

static void runnerStop(runnerData *r) {
#ifndef _WIN32
	pthread_mutex_lock(&r->lock);
	int running = r->running;
	pid_t pid = r->pid;
	pthread_mutex_unlock(&r->lock);
	if (running && pid > 0 && kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);   /* its group: a pipeline dies whole */
	if (r->thread_valid) {
		pthread_join(r->thread, NULL);
		r->thread_valid = 0;
	}
	if (r->fp) {
		fclose(r->fp);
		r->fp = NULL;
	}
	r->pid = -1;
#else
	(void)r;
#endif
}

static void runnerCleanup(editorPane *pane) {
	runnerData *r = pane->runner;
	if (!r) return;
	runnerStop(r);
	for (int i = 0; i < r->count; i++) free(r->lines[i]);
	free(r->lines);
	for (int i = 0; i < r->hist_n; i++) free(r->hist[i]);
	free(r->hist);
	pthread_mutex_destroy(&r->lock);
	free(r);
	pane->runner = NULL;
}

static runnerData *runnerEnsurePane(void) {
	if (!E.bottom_panel) {
		E.bottom_panel = editorCreatePane(PANE_RUNNER, 0, 0, E.screencols, 10);
		if (!E.bottom_panel) return NULL;
		E.bottom_panel->runner = calloc(1, sizeof(runnerData));
		if (!E.bottom_panel->runner) {
			editorFreePane(E.bottom_panel);
			E.bottom_panel = NULL;
			return NULL;
		}
		pthread_mutex_init(&E.bottom_panel->runner->lock, NULL);
		if (!getcwd(E.bottom_panel->runner->cwd, sizeof(E.bottom_panel->runner->cwd)))
			snprintf(E.bottom_panel->runner->cwd, sizeof(E.bottom_panel->runner->cwd), ".");
		editorResizePanes();
	}
	return E.bottom_panel->runner;
}

#ifndef _WIN32
static char *hkFindOnPath(const char *name) {
	const char *path = getenv("PATH");
	char cand[PATH_MAX];
	while (path && *path) {
		const char *colon = strchr(path, ':');
		int seg = colon ? (int)(colon - path) : (int)strlen(path);
		if (seg > 0 && snprintf(cand, sizeof(cand), "%.*s/%s", seg, path, name) < (int)sizeof(cand) &&
		    access(cand, X_OK) == 0) return strdup(cand);
		if (!colon) break;
		path = colon + 1;
	}
	return NULL;
}
#endif

#ifndef _WIN32
/* console_sandbox=1: writes only to the project and $TMPDIR, no network. macOS
   has sandbox-exec, Linux bwrap; anywhere else the command does not run. */
static int hkSandboxArgv(const char *cmd, const char *cwd, char **argv, char *profile, size_t psz, char *errbuf, size_t esz) {
	static char proj[PATH_MAX], tmp[PATH_MAX], bin[PATH_MAX];
	const char *t = getenv("TMPDIR");
	if (!realpath(E.project_dir ? E.project_dir : ".", proj) || !realpath(t && *t ? t : "/tmp", tmp)) {
		snprintf(errbuf, esz, "console_sandbox=1: cannot resolve the project or TMPDIR");
		return 0;
	}
	size_t tl = strlen(tmp);
	if (tl > 1 && tmp[tl - 1] == '/') tmp[tl - 1] = '\0';
	if (strpbrk(proj, "\"\\") || strpbrk(tmp, "\"\\")) {    /* a quote in a path would rewrite the profile */
		snprintf(errbuf, esz, "console_sandbox=1: the project path has a quote or backslash, so it does not run");
		return 0;
	}
	if (access("/usr/bin/sandbox-exec", X_OK) == 0) {
		snprintf(profile, psz, "(version 1)(allow default)(deny network*)(deny file-write*)"
			"(allow file-write* (subpath \"%s\") (subpath \"%s\") (subpath \"/dev\"))", proj, tmp);
		int i = 0;
		argv[i++] = "/usr/bin/sandbox-exec"; argv[i++] = "-p"; argv[i++] = profile;
		argv[i++] = "/bin/sh"; argv[i++] = "-c"; argv[i++] = (char *)cmd; argv[i] = NULL;
		return 1;
	}
	char *bw = hkFindOnPath("bwrap");
	if (bw) {
		snprintf(bin, sizeof(bin), "%s", bw);
		free(bw);
		int i = 0;
		argv[i++] = bin; argv[i++] = "--ro-bind"; argv[i++] = "/"; argv[i++] = "/";
		argv[i++] = "--bind"; argv[i++] = proj; argv[i++] = proj;
		argv[i++] = "--bind"; argv[i++] = tmp; argv[i++] = tmp;
		argv[i++] = "--dev"; argv[i++] = "/dev"; argv[i++] = "--proc"; argv[i++] = "/proc";
		argv[i++] = "--unshare-net"; argv[i++] = "--die-with-parent"; argv[i++] = "--new-session";
		argv[i++] = "--chdir"; argv[i++] = (char *)cwd;
		argv[i++] = "/bin/sh"; argv[i++] = "-c"; argv[i++] = (char *)cmd; argv[i] = NULL;
		return 1;
	}
	snprintf(errbuf, esz, "console_sandbox=1: no sandbox-exec or bwrap here, so it does not run unsandboxed");
	return 0;
}

#endif

/* keep: the console adds to what is there; the runner starts clean */
static void runnerSpawn(const char *cmd, int keep, int sandbox) {
#ifdef _WIN32
	(void)cmd; (void)keep; (void)sandbox;
	editorSetStatusMessage(":run and :console are not wired on Windows yet; use :!<cmd>");
#else
	runnerData *r = runnerEnsurePane();
	if (!r) return;
	runnerStop(r);

	char *argv[24], profile[PATH_MAX * 2 + 256], err[160];
	if (sandbox && !hkSandboxArgv(cmd, r->cwd, argv, profile, sizeof(profile), err, sizeof(err))) {
		runnerAppend(r, err);
		editorSetStatusMessage("%s", err);
		return;
	}
	pthread_mutex_lock(&r->lock);
	if (!keep) {
		for (int i = 0; i < r->count; i++) free(r->lines[i]);
		r->count = 0;
		r->selected = 0;
		r->scroll_offset = 0;
	}
	r->truncated = 0;
	r->exit_code = -1;
	snprintf(r->cmd, sizeof(r->cmd), "%s", cmd);
	pthread_mutex_unlock(&r->lock);

	int p[2];
	if (pipe(p) < 0) {
		editorSetStatusMessage("pipe failed: %s", strerror(errno));
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		editorSetStatusMessage("fork failed: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		/* sandboxed: a new session, so it cannot open the terminal and push
		   keystrokes into the editor (TIOCSTI) */
		if (sandbox) setsid(); else setpgid(0, 0);
		int devnull = open("/dev/null", O_RDONLY);     /* never the editor's keyboard */
		if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
		close(p[0]);
		close(p[1]);
		if (r->cwd[0] && chdir(r->cwd) != 0) _exit(126);
		if (sandbox) execv(argv[0], argv);
		else execl("/bin/sh", "sh", "-c", r->cmd, (char *)NULL);
		_exit(127);
	}
	if (!sandbox) setpgid(pid, pid);       /* (setsid needs it not to be a group leader yet) */
	close(p[1]);
	r->fp = fdopen(p[0], "r");
	r->pid = pid;
	r->running = 1;
	if (pthread_create(&r->thread, NULL, runnerThread, r) != 0) {
		fclose(r->fp);
		r->fp = NULL;
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		r->running = 0;
		editorSetStatusMessage("thread failed");
		return;
	}
	r->thread_valid = 1;
	if (!r->console) editorSetStatusMessage("▶ %s", r->cmd);
#endif
}

static void runnerStart(const char *cmd) {
	runnerSpawn(cmd, E.bottom_panel && E.bottom_panel->runner && E.bottom_panel->runner->console, 0);
}

/* :console: a shell line under the runner's output, in the project dir */
static void hkConsoleToggle(void) {
	if (E.bottom_panel && E.bottom_panel->runner && E.bottom_panel->runner->console) {
		hkCloseSidePanel(&E.bottom_panel, NULL);
		return;
	}
	runnerData *r = runnerEnsurePane();
	if (!r) return;
	r->console = 1;
	if (E.active_pane) E.active_pane->is_focused = 0;
	E.active_pane = E.bottom_panel;
	E.bottom_panel->is_focused = 1;
	editorSetStatusMessage("console: %s%s", r->cwd, E.console_sandbox ? " (sandboxed)" : "");
}

/* dir/name into dst, or 0 when it would not fit (never a clipped path) */
static int hkFmtFits(char *dst, size_t cap, const char *dir, const char *name) {
	size_t a = strlen(dir), b = strlen(name);
	if (a + 1 + b + 1 > cap) return 0;
	memcpy(dst, dir, a);
	dst[a] = '/';
	memcpy(dst + a + 1, name, b + 1);
	return 1;
}

static void hkConsoleRun(runnerData *r) {
	char line[sizeof(r->input)];
	snprintf(line, sizeof(line), "%s", r->input);
	r->input_len = r->input_cur = 0;
	r->input[0] = '\0';
	const char *c = line;
	while (*c == ' ') c++;
	if (!*c) return;
	if (r->hist_n == 0 || strcmp(r->hist[r->hist_n - 1], c) != 0) {
		char **nh = realloc(r->hist, sizeof(char *) * (r->hist_n + 1));
		if (nh) { r->hist = nh; r->hist[r->hist_n++] = strdup(c); }
	}
	r->hist_pos = r->hist_n;
	char echo[sizeof(line) + 4];
	snprintf(echo, sizeof(echo), "$ %s", c);
	runnerAppend(r, echo);
	if (!strcmp(c, "clear")) {
		pthread_mutex_lock(&r->lock);
		for (int i = 0; i < r->count; i++) free(r->lines[i]);
		r->count = r->selected = r->scroll_offset = 0;
		pthread_mutex_unlock(&r->lock);
		return;
	}
	if (!strcmp(c, "exit")) { hkCloseSidePanel(&E.bottom_panel, NULL); return; }
	if (!strcmp(c, "cd") || !strncmp(c, "cd ", 3)) {
		const char *d = c[2] ? c + 3 : "";
		while (*d == ' ') d++;
		char want[PATH_MAX], real[PATH_MAX];
		if (!*d || *d == '~') snprintf(want, sizeof(want), "%s%s", getenv("HOME") ? getenv("HOME") : "/", *d ? d + 1 : "");
		else if (*d == '/') snprintf(want, sizeof(want), "%s", d);
		else if (!hkFmtFits(want, sizeof(want), r->cwd, d)) { runnerAppend(r, "cd: path too long"); return; }
		struct stat st;
		if (realpath(want, real) && stat(real, &st) == 0 && S_ISDIR(st.st_mode)) snprintf(r->cwd, sizeof(r->cwd), "%s", real);
		else runnerAppend(r, "cd: no such directory");
		return;
	}
	runnerSpawn(c, 1, E.console_sandbox);
}

static void hkConsoleKey(editorPane *pane, runnerData *r, int key) {
	(void)pane;
	pthread_mutex_lock(&r->lock);
	int running = r->running;
	pthread_mutex_unlock(&r->lock);
	switch (key) {
	case '\r':
		if (running) { editorSetStatusMessage("still running: ctrl-c stops it"); break; }
		hkConsoleRun(r);
		break;
	case CTRL_KEY('c'):
		if (running) { runnerStop(r); runnerAppend(r, "^C"); }
		else { r->input_len = r->input_cur = 0; r->input[0] = '\0'; }
		break;
	case '\x1b': {
		editorPane *ed = hkFirstEditorLeaf();             /* back to the file; the console stays */
		if (ed) { pane->is_focused = 0; E.active_pane = ed; ed->is_focused = 1; }
		break;
	}
	case BACKSPACE: case CTRL_KEY('h'):
		if (r->input_cur > 0) {
			memmove(r->input + r->input_cur - 1, r->input + r->input_cur, r->input_len - r->input_cur + 1);
			r->input_cur--; r->input_len--;
		}
		break;
	case DEL_KEY: case CTRL_KEY('d'):
		if (r->input_cur < r->input_len) {
			memmove(r->input + r->input_cur, r->input + r->input_cur + 1, r->input_len - r->input_cur);
			r->input_len--;
		}
		break;
	case ARROW_LEFT: case CTRL_KEY('b'): if (r->input_cur > 0) r->input_cur--; break;
	case ARROW_RIGHT: case CTRL_KEY('f'): if (r->input_cur < r->input_len) r->input_cur++; break;
	case HOME_KEY: case CTRL_KEY('a'): r->input_cur = 0; break;
	case END_KEY: case CTRL_KEY('e'): r->input_cur = r->input_len; break;
	case CTRL_KEY('u'): r->input_len = r->input_cur = 0; r->input[0] = '\0'; break;
	case ARROW_UP: case ARROW_DOWN: case CTRL_KEY('p'): case CTRL_KEY('n'): {
		int up = key == ARROW_UP || key == CTRL_KEY('p');
		if (up && r->hist_pos > 0) r->hist_pos--;
		else if (!up && r->hist_pos < r->hist_n) r->hist_pos++;
		else break;
		const char *h = r->hist_pos < r->hist_n ? r->hist[r->hist_pos] : "";
		snprintf(r->input, sizeof(r->input), "%s", h);
		r->input_len = r->input_cur = (int)strlen(r->input);
		break;
	}
	case PAGE_UP: case PAGE_DOWN:
		pthread_mutex_lock(&r->lock);
		r->selected += key == PAGE_UP ? -(pane->height - 2) : pane->height - 2;
		if (r->selected < 0) r->selected = 0;
		if (r->selected >= r->count) r->selected = r->count ? r->count - 1 : 0;
		pthread_mutex_unlock(&r->lock);
		break;
	default:
		if (key >= 32 && key < 256 && key != 127 && r->input_len < (int)sizeof(r->input) - 1) {
			memmove(r->input + r->input_cur + 1, r->input + r->input_cur, r->input_len - r->input_cur + 1);
			r->input[r->input_cur++] = (char)key;
			r->input_len++;
		}
		break;
	}
}

static void runnerErrorStep(int dir) {
	if (!E.bottom_panel || !E.bottom_panel->runner) {
		editorSetStatusMessage("No runner output — :build / :run first");
		return;
	}
	runnerData *r = E.bottom_panel->runner;
	char path[PATH_MAX];
	int lno = 0, coln = 0, found = -1;
	pthread_mutex_lock(&r->lock);
	for (int i = r->selected + dir; i >= 0 && i < r->count; i += dir) {
		if (runnerParseError(r->lines[i], path, sizeof(path), &lno, &coln)) {
			found = i;
			break;
		}
	}
	if (found >= 0) r->selected = found;
	pthread_mutex_unlock(&r->lock);
	if (found >= 0) runnerJumpTo(path, lno, coln);
	else editorSetStatusMessage("No %s error", dir > 0 ? "next" : "previous");
}

static void runnerAskRei(runnerData *r) {
	char line_copy[512] = "";
	char cmd_copy[512];
	pthread_mutex_lock(&r->lock);
	if (r->selected >= 0 && r->selected < r->count)
		snprintf(line_copy, sizeof(line_copy), "%s", r->lines[r->selected]);
	snprintf(cmd_copy, sizeof(cmd_copy), "%s", r->cmd);
	pthread_mutex_unlock(&r->lock);
	if (!line_copy[0]) {
		editorSetStatusMessage("Nothing selected to send");
		return;
	}
	if (!E.right_panel || E.right_panel->type != PANE_AI) editorToggleAI();
	if (!E.right_panel || !E.right_panel->ai) return;
	aiData *ai = E.right_panel->ai;
	char prompt[1200];
	snprintf(prompt, sizeof(prompt), "Fix this error from `%s`:\n%s", cmd_copy, line_copy);
	pthread_mutex_lock(&ai->lock);
	aiAddHistoryRole(ai, prompt, HK_ROLE_USER);
	free(ai->current_prompt);
	ai->current_prompt = strdup(prompt);
	pthread_mutex_unlock(&ai->lock);
	aiWorkerSend(ai);
	editorSetStatusMessage("Sent to %s", E.ai_name);
}

void runnerRender(editorPane *pane, struct abuf *ab) {
	(void)ab;
	runnerData *r = pane->runner;
	if (!r || pane->height <= 0) return;
	pthread_mutex_lock(&r->lock);

	char header[PATH_MAX + 64];
	if (r->console)
		snprintf(header, sizeof(header), " console  %.200s%s%s", r->cwd, E.console_sandbox ? "  [sandboxed]" : "",
			r->running ? "  [running: ctrl-c stops]" : "");
	else if (r->running)
		snprintf(header, sizeof(header), " ▶ %.180s  [running]", r->cmd);
	else
		snprintf(header, sizeof(header), " ▶ %.140s  [exit %d]  enter=jump  a=%s  r=rerun  q=close",
			r->cmd, r->exit_code, E.ai_name);
	int used = gridPutStr(pane->x, pane->y, header, pane->width, E.theme.status_fg, E.theme.status_bg);
	for (int x = used; x < pane->width; x++)
		gridPutCh(pane->x + x, pane->y, ' ', E.theme.status_fg, E.theme.status_bg);

	int body = pane->height - 1 - (r->console ? 1 : 0);
	if (r->console && body > 0) {            /* the prompt line under the output */
		int gy = pane->y + pane->height - 1;
		Color pc = E.theme.keyword1;
		int u = gridPutStr(pane->x, gy, r->running ? "  " : "$ ", 2, pc, E.theme.bg);
		u += gridPutStr(pane->x + u, gy, r->input, pane->width - u, E.theme.fg, E.theme.bg);
		for (int x = u; x < pane->width; x++) gridPutCh(pane->x + x, gy, ' ', E.theme.fg, E.theme.bg);
	}
	if (r->selected >= r->count) r->selected = r->count ? r->count - 1 : 0;
	if (r->selected < 0) r->selected = 0;
	if (r->selected < r->scroll_offset) r->scroll_offset = r->selected;
	if (body > 0 && r->selected >= r->scroll_offset + body) r->scroll_offset = r->selected - body + 1;
	if (r->scroll_offset < 0) r->scroll_offset = 0;

	char path[PATH_MAX];
	int lno, coln;
	for (int y = 0; y < body; y++) {
		int idx = r->scroll_offset + y;
		int gy = pane->y + 1 + y;
		if (idx < r->count) {
			Color fg = runnerParseError(r->lines[idx], path, sizeof(path), &lno, &coln)
				? E.theme.keyword1 : E.theme.fg;
			Color bg = (idx == r->selected && pane == E.active_pane) ? E.theme.visual_bg : E.theme.bg;
			int u = gridPutStr(pane->x + 1, gy, r->lines[idx], pane->width - 1, fg, bg);
			gridPutCh(pane->x, gy, ' ', fg, bg);
			for (int x = 1 + u; x < pane->width; x++) gridPutCh(pane->x + x, gy, ' ', fg, bg);
		} else {
			for (int x = 0; x < pane->width; x++) gridPutCh(pane->x + x, gy, ' ', E.theme.fg, E.theme.bg);
		}
	}
	pthread_mutex_unlock(&r->lock);
}

void runnerHandleKey(editorPane *pane, int key) {
	runnerData *r = pane->runner;
	if (!r) return;
	if (r->console) { hkConsoleKey(pane, r, key); return; }
	switch (key) {
	case 'j':
	case ARROW_DOWN:
		pthread_mutex_lock(&r->lock);
		if (r->selected < r->count - 1) r->selected++;
		pthread_mutex_unlock(&r->lock);
		break;

	case 'k':
	case ARROW_UP:
		pthread_mutex_lock(&r->lock);
		if (r->selected > 0) r->selected--;
		pthread_mutex_unlock(&r->lock);
		break;

	case 'g':
		pthread_mutex_lock(&r->lock);
		r->selected = 0;
		r->scroll_offset = 0;
		pthread_mutex_unlock(&r->lock);
		break;

	case 'G':
		pthread_mutex_lock(&r->lock);
		r->selected = r->count ? r->count - 1 : 0;
		pthread_mutex_unlock(&r->lock);
		break;

	case '\r': {
		char path[PATH_MAX];
		int lno = 0, coln = 0, ok = 0;
		pthread_mutex_lock(&r->lock);
		if (r->selected < r->count)
			ok = runnerParseError(r->lines[r->selected], path, sizeof(path), &lno, &coln);
		pthread_mutex_unlock(&r->lock);
		if (ok) runnerJumpTo(path, lno, coln);
		else editorSetStatusMessage("No file:line on this line");
		break;
	}

	case ']':
		runnerErrorStep(1);
		break;
	case '[':
		runnerErrorStep(-1);
		break;

	case 'r': {
		char c[512];
		pthread_mutex_lock(&r->lock);
		int running = r->running;
		snprintf(c, sizeof(c), "%s", r->cmd);
		pthread_mutex_unlock(&r->lock);
		if (running) editorSetStatusMessage("Still running — q stops it first");
		else if (*c) runnerStart(c);
		break;
	}

	case 'a':
		runnerAskRei(r);
		break;

	case ':':
		editorColonCommand();
		break;

	case 'q':
	case '\x1b': {
		pthread_mutex_lock(&r->lock);
		int running = r->running;
		pthread_mutex_unlock(&r->lock);
		if (running) {
			runnerStop(r);
			editorSetStatusMessage("Stopped: %s", r->cmd);
		} else {
			hkCloseSidePanel(&E.bottom_panel, NULL);
		}
		break;
	}
	}
}

static void hkShellPopup(const char *cmdline) {
	while (*cmdline == ' ') cmdline++;
	if (!*cmdline) {
		editorSetStatusMessage("Usage: :!<cmd>");
		return;
	}
	char full[1024];
	snprintf(full, sizeof(full), "(%s) 2>&1", cmdline);
	FILE *fp = popen(full, "r");
	if (!fp) {
		editorSetStatusMessage("Could not run: %s", cmdline);
		return;
	}
	char **items = NULL;
	int n = 0, cap = 0;
	char ln[512];
	while (fgets(ln, sizeof(ln), fp)) {
		ln[strcspn(ln, "\r\n")] = '\0';
		if (n >= 400) break;
		if (n >= cap) {
			cap = cap ? cap * 2 : 64;
			items = realloc(items, cap * sizeof(char *));
		}
		items[n++] = strdup(ln);
	}
	int rc = pclose(fp);
#ifdef _WIN32
	int code = rc;
#else
	int code = (rc == -1) ? -1 : WEXITSTATUS(rc);
#endif
	char title[80];
	snprintf(title, sizeof(title), "! exit %d — %.30s", code, cmdline);
	if (n == 0) editorSetStatusMessage("%s (no output)", title);
	else editorPopupOpen(title, (const char *const *)items, n, 0, 0);
	for (int i = 0; i < n; i++) free(items[i]);
	free(items);
}

/*** AI assistant ***/
void editorInitAI() {
	E.hako_protocol = 1;
}

void editorCleanupAI() {
	free(E.hako_session_id);
	E.hako_session_id = NULL;
}

void aiInit(editorPane *pane) {
	aiData *data = calloc(1, sizeof(aiData));
	if (!data) return;

	data->history = malloc(sizeof(char*) * AI_HISTORY_MAX);
	data->history_role = calloc(AI_HISTORY_MAX, 1);
	data->visual_start = -1;
	data->visual_end = -1;
	data->prompt_buffer = malloc(256);
	data->prompt_buffer[0] = '\0';
	data->prompt_capacity = 256;
	pthread_mutex_init(&data->lock, NULL);

	pane->ai = data;

	int rc = hakoLaunch(data);
	if (rc == -3) {
		data->history[data->history_count++] = strdup("This build ships without an agent.");
		data->history[data->history_count++] = strdup("Editing works fully; the Rei pane is inactive.");
		data->history[data->history_count++] = strdup("Get the full build: https://mithraeums.github.io/hake.sh");
	} else if (rc == -2) {
		data->history[data->history_count++] = strdup("No hako agent found on this system.");
		data->history[data->history_count++] = strdup(":install-agent   install it now, in place");
		data->history[data->history_count++] = strdup("or: curl -fsSL https://mithraeums.github.io/hako.sh | sh");
		data->history[data->history_count++] = strdup("then 'i' to retry — :pull hako-sho for a local model.");
	} else if (rc < 0) {
		data->history[data->history_count++] = strdup("Could not start the agent (launch error).");
		data->history[data->history_count++] = strdup("'i' to try again | :install-agent | /help");
	} else {
		data->history[data->history_count++] = strdup("i to type \xC2\xB7 : or / for hako's commands");
	}
}

static void aiPromptInsert(aiData *data, char c) {
	if (data->prompt_len >= data->prompt_capacity - 1) {
		data->prompt_capacity *= 2;
		data->prompt_buffer = realloc(data->prompt_buffer, data->prompt_capacity);
	}
	memmove(data->prompt_buffer + data->prompt_cursor + 1,
		data->prompt_buffer + data->prompt_cursor,
		data->prompt_len - data->prompt_cursor + 1);
	data->prompt_buffer[data->prompt_cursor++] = c;
	data->prompt_len++;
}

static int aiSubmitPrompt(aiData *data) {
	if (!data || data->prompt_len == 0) return 0;
	/* hako takes `:cmd` (vim habit) as well as `/cmd`. The pane only knew
	   `/`, so `:models` or `:help` typed here went to the MODEL as chat, and a
	   small model answered with an invented command list. Same prefixes here. */
	if (data->prompt_buffer[0] == '/' || data->prompt_buffer[0] == ':') {
		int r = hkHandleSlash(data, data->prompt_buffer);
		data->prompt_buffer[0] = '\0';
		data->prompt_len = 0;
		data->prompt_cursor = 0;
		data->mode = MODE_NORMAL;
		editorSetStatusMessage("-- AI NORMAL --");
		return r;
	}
	aiAddHistoryRole(data, data->prompt_buffer, HK_ROLE_USER);
	free(data->current_prompt);
	data->current_prompt = strdup(data->prompt_buffer);
	data->prompt_buffer[0] = '\0';
	data->prompt_len = 0;
	data->prompt_cursor = 0;
	data->mode = MODE_NORMAL;
	editorSetStatusMessage("-- AI NORMAL --");
	if (data->streaming) {
		aiAddHistory(data, "Waiting for response...");
		return 1;
	}
	return 3;
}

void aiHandleKey(editorPane *pane, int key) {
	aiData *data = pane->ai;
	if (!data) return;

	pthread_mutex_lock(&data->lock);

	if (data->perm_pending) {
		const char *dec = NULL;
		if      (key == 'y' || key == 'Y') dec = "y";
		else if (key == 'a' || key == 'A') dec = "a";
		else if (key == 'n' || key == 'N' || key == '\x1b') dec = "n";
		if (dec) {
			char json[96];
			snprintf(json, sizeof(json),
				"{\"type\":\"approve\",\"id\":%d,\"answer\":\"%s\"}",
				data->perm_id, dec);
			data->perm_pending = 0;
			aiAddHistory(data, dec[0] == 'n' ? "  denied" :
			                   dec[0] == 'a' ? "  allowed (always)" : "  allowed");
			pthread_mutex_unlock(&data->lock);
			hakoSendLine(json);
			return;
		}
		pthread_mutex_unlock(&data->lock);
		return;
	}

	if (data->mode == MODE_INSERT) {
		if (data->prompt_cursor < 0) data->prompt_cursor = 0;
		if (data->prompt_cursor > data->prompt_len) data->prompt_cursor = data->prompt_len;
		int menu[64];
		int nmenu = hkReiMenuRows(pane, hkReiMatches(data, menu, 64)) ? hkReiMatches(data, menu, 64) : 0;
		if (nmenu && (key == ARROW_UP || key == ARROW_DOWN)) {
			data->cmd_sel += key == ARROW_DOWN ? 1 : -1;
			if (data->cmd_sel < 0) data->cmd_sel = nmenu - 1;
			if (data->cmd_sel >= nmenu) data->cmd_sel = 0;
			pthread_mutex_unlock(&data->lock);
			return;
		}
		if (nmenu && key == '\t') {                     /* complete the picked command */
			const hkCmd *c = &hk_cmds[menu[data->cmd_sel < nmenu ? data->cmd_sel : 0]];
			int l = snprintf(data->prompt_buffer, data->prompt_capacity, "%s%s%s", c->p, c->n, c->a[0] ? " " : "");
			if (l >= data->prompt_capacity) l = data->prompt_capacity - 1;
			data->prompt_len = data->prompt_cursor = l;
			data->cmd_sel = 0;
			pthread_mutex_unlock(&data->lock);
			return;
		}
		/* a : or / line is one line: enter runs it (the picked entry when what is
		   typed is the start of its name) */
		if (key == '\r' && data->prompt_len > 1 && (data->prompt_buffer[0] == ':' || data->prompt_buffer[0] == '/') &&
		    !strchr(data->prompt_buffer, '\n')) {
			if (nmenu) {
				const hkCmd *c = &hk_cmds[menu[data->cmd_sel < nmenu ? data->cmd_sel : 0]];
				if (!strncmp(c->n, data->prompt_buffer + 1, strlen(data->prompt_buffer + 1))) {
					int l = snprintf(data->prompt_buffer, data->prompt_capacity, "%s%s", c->p, c->n);
					if (l >= data->prompt_capacity) l = data->prompt_capacity - 1;
					data->prompt_len = data->prompt_cursor = l;
				}
			}
			data->cmd_sel = 0;
			data->cmd_hide = 0;
			int r = aiSubmitPrompt(data);
			pthread_mutex_unlock(&data->lock);
			if (r == 2) editorToggleAI();
			return;
		}
		if (key != ARROW_LEFT && key != ARROW_RIGHT && key != HOME_KEY && key != END_KEY && key != '\x1b') {
			data->cmd_hide = 0;
			data->cmd_sel = 0;
		}
		switch (key) {
		case '\x1b':
			if (nmenu) { data->cmd_hide = 1; break; }   /* the first esc closes the menu only */
			data->mode = MODE_NORMAL;
			editorSetStatusMessage("-- AI NORMAL --");
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
			if (data->prompt_cursor > 0) {
				memmove(data->prompt_buffer + data->prompt_cursor - 1,
					data->prompt_buffer + data->prompt_cursor,
					data->prompt_len - data->prompt_cursor + 1);
				data->prompt_cursor--;
				data->prompt_len--;
			}
			break;

		case DEL_KEY:
			if (data->prompt_cursor < data->prompt_len) {
				memmove(data->prompt_buffer + data->prompt_cursor,
					data->prompt_buffer + data->prompt_cursor + 1,
					data->prompt_len - data->prompt_cursor);
				data->prompt_len--;
			}
			break;

		case ARROW_LEFT:
			if (data->prompt_cursor > 0) data->prompt_cursor--;
			break;
		case ARROW_RIGHT:
			if (data->prompt_cursor < data->prompt_len) data->prompt_cursor++;
			break;
		case ARROW_UP: {
			int line_start = data->prompt_cursor;
			while (line_start > 0 && data->prompt_buffer[line_start - 1] != '\n') line_start--;
			int col = data->prompt_cursor - line_start;
			if (line_start == 0) break;
			int prev_end = line_start - 1;
			int prev_start = prev_end;
			while (prev_start > 0 && data->prompt_buffer[prev_start - 1] != '\n') prev_start--;
			int prev_len = prev_end - prev_start;
			data->prompt_cursor = prev_start + (col < prev_len ? col : prev_len);
			break;
		}
		case ARROW_DOWN: {
			int line_start = data->prompt_cursor;
			while (line_start > 0 && data->prompt_buffer[line_start - 1] != '\n') line_start--;
			int col = data->prompt_cursor - line_start;
			int line_end = data->prompt_cursor;
			while (line_end < data->prompt_len && data->prompt_buffer[line_end] != '\n') line_end++;
			if (line_end >= data->prompt_len) break;
			int next_start = line_end + 1;
			int next_end = next_start;
			while (next_end < data->prompt_len && data->prompt_buffer[next_end] != '\n') next_end++;
			int next_len = next_end - next_start;
			data->prompt_cursor = next_start + (col < next_len ? col : next_len);
			break;
		}
		case HOME_KEY: {
			int ls = data->prompt_cursor;
			while (ls > 0 && data->prompt_buffer[ls - 1] != '\n') ls--;
			data->prompt_cursor = ls;
			break;
		}
		case END_KEY: {
			int le = data->prompt_cursor;
			while (le < data->prompt_len && data->prompt_buffer[le] != '\n') le++;
			data->prompt_cursor = le;
			break;
		}

		case '\r':
		case '\n':
			aiPromptInsert(data, '\n');
			break;

		default:
			if (!iscntrl(key) && key < 128) aiPromptInsert(data, (char)key);
			break;
		}
	} else if (data->visual_mode) {
		switch (key) {
		case '\x1b':
			data->visual_mode = 0;
			data->visual_start = data->visual_end = -1;
			editorSetStatusMessage("-- AI NORMAL --");
			break;
			
		case 'j':
		case ARROW_DOWN:
			if (data->cursor_y < data->history_count - 1) {
				data->cursor_y++;
				data->visual_end = data->cursor_y;
			}
			break;
			
		case 'k':
		case ARROW_UP:
			if (data->cursor_y > 0) {
				data->cursor_y--;
				data->visual_end = data->cursor_y;
			}
			break;
			
		case 'y':
			{
				int start = MIN(data->visual_start, data->visual_end);
				int end = MAX(data->visual_start, data->visual_end);
				
				size_t total_len = 0;
				for (int i = start; i <= end && i < data->history_count; i++) {
					total_len += strlen(data->history[i]) + 1;
				}
				
				if (total_len > 0) {
					char *yanked = malloc(total_len + 1);
					char *p = yanked;
					
					for (int i = start; i <= end && i < data->history_count; i++) {
						int len = strlen(data->history[i]);
						memcpy(p, data->history[i], len);
						p += len;
						*p++ = '\n';
					}
					*p = '\0';
					
					editorSetPasteBuffer(yanked, p - yanked, 1);
					free(yanked);
					editorSetStatusMessage("Yanked %d lines", end - start + 1);
				}
				
				data->visual_mode = 0;
				data->visual_start = data->visual_end = -1;
			}
			break;
			
		case CTRL_KEY('w'):
			data->visual_mode = 0;
			pthread_mutex_unlock(&data->lock);
			editorNextPane();
			return;

		case 'g':
			data->cursor_y = 0;
			data->visual_end = 0;
			data->scroll_offset = 1000000;
			break;

		case 'G':
			data->cursor_y = data->history_count > 0 ? data->history_count - 1 : 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset = 0;
			break;

		case CTRL_KEY('d'):
		case CTRL_KEY('f'): {
			int step = pane->height / 2;
			if (step < 1) step = 1;
			data->cursor_y += step;
			if (data->cursor_y > data->history_count - 1)
				data->cursor_y = data->history_count > 0 ? data->history_count - 1 : 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset -= step;
			if (data->scroll_offset < 0) data->scroll_offset = 0;
			break;
		}

		case CTRL_KEY('u'):
		case CTRL_KEY('b'): {
			int step = pane->height / 2;
			if (step < 1) step = 1;
			data->cursor_y -= step;
			if (data->cursor_y < 0) data->cursor_y = 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset += step;
			break;
		}

		case 'V':
			data->visual_start = data->visual_end = data->cursor_y;
			break;

		case CTRL_KEY('c'):
			data->visual_mode = 0;
			data->visual_start = data->visual_end = -1;
			editorSetStatusMessage("-- AI NORMAL --");
			break;
		}
	} else {
		switch (key) {
		case 'i':
			data->mode = MODE_INSERT;
			editorSetStatusMessage("-- AI INSERT -- (Esc then Enter to send)");
			break;

		case 'v':
			data->visual_mode = 1;
			data->visual_start = data->visual_end = data->cursor_y;
			editorSetStatusMessage("-- AI VISUAL --");
			break;

		case '\r':
		case '\n':
			if (data->prompt_len > 0) {
				int r = aiSubmitPrompt(data);
				if (r == 2) {
					pthread_mutex_unlock(&data->lock);
					editorToggleAI();
					return;
				}
				if (r == 3) {
					pthread_mutex_unlock(&data->lock);
					aiWorkerSend(data);
					return;
				}
			}
			break;
			
		case 'j':
		case ARROW_DOWN:
			if (data->scroll_offset > 0) data->scroll_offset--;
			break;

		case 'k':
		case ARROW_UP:
			data->scroll_offset++;
			break;

		case CTRL_KEY('d'):
			data->scroll_offset -= pane->height / 2;
			if (data->scroll_offset < 0) data->scroll_offset = 0;
			break;

		case CTRL_KEY('u'):
			data->scroll_offset += pane->height / 2;
			break;
			
		case 'g':
			data->scroll_offset = 1000000;
			break;

		case 'G':
			data->scroll_offset = 0;
			break;
			
		case ':':
			{
				pthread_mutex_unlock(&data->lock);
				char *cmd = editorPrompt(":%s", NULL);
				pthread_mutex_lock(&data->lock);
				if (cmd) {
					if (strcmp(cmd, "q") == 0) {
						free(cmd);
						pthread_mutex_unlock(&data->lock);
						editorToggleAI();
						return;
					} else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "wq") == 0) {
						FILE *fp = fopen("ai_chat.txt", "w");
						if (fp) {
							for (int i = 0; i < data->history_count; i++) {
								fprintf(fp, "%s\n", data->history[i]);
							}
							fclose(fp);
							editorSetStatusMessage("AI chat saved to ai_chat.txt");
						}
						if (strcmp(cmd, "wq") == 0) {
							free(cmd);
							pthread_mutex_unlock(&data->lock);
							editorToggleAI();
							return;
						}
					} else if (strcmp(cmd, "clear") == 0) {
						for (int i = 0; i < data->history_count; i++) {
							free(data->history[i]);
						}
						data->history_count = 0;
						data->cursor_y = 0;
						editorSetStatusMessage("AI history cleared");
					} else if (strcmp(cmd, "auto") == 0) {
						data->perm_pending = 0;
						hakoSendLine("{\"type\":\"slash\",\"cmd\":\":auto on\"}");
						aiAddHistory(data, "auto-approve ON — tools run without prompting (:noauto to undo).");
					} else if (strcmp(cmd, "noauto") == 0) {
						hakoSendLine("{\"type\":\"slash\",\"cmd\":\":auto off\"}");
						aiAddHistory(data, "auto-approve OFF — prompt per tool call (:auto to skip).");
					} else if (strcmp(cmd, "install-agent") == 0 ||
					           strcmp(cmd, "update-agent")  == 0) {
						int upd = (cmd[0] == 'u');
						free(cmd);
						pthread_mutex_unlock(&data->lock);
						hakoInstallOrUpdate(data, upd);
						return;
					} else {
						/* Everything else is the editor's: it used to vanish
						   silently while Rei had focus. hako's own commands are
						   typed in the input (i, then :help). */
						pthread_mutex_unlock(&data->lock);
						editorRunColon(cmd);
						return;
					}
					free(cmd);
				}
			}
			break;
			
		case CTRL_KEY('w'):
			pthread_mutex_unlock(&data->lock);
			editorNextPane();
			return;

		case CTRL_KEY('c'):
			pthread_mutex_unlock(&data->lock);
			editorToggleAI();
			return;
		}
	}

	pthread_mutex_unlock(&data->lock);
}

void editorToggleAI() {
	if (E.right_panel && E.right_panel->type == PANE_AI) {
		hkCloseSidePanel(&E.right_panel, E.left_panel);
		return;
	}
	
	E.right_panel_width = E.explorer_width > 0 ? E.explorer_width : 30;
	E.right_panel = editorCreatePane(PANE_AI, E.screencols - E.right_panel_width, 0, E.right_panel_width, E.screenrows);
	E.right_panel->wrap_lines = 1;
	aiInit(E.right_panel);

	editorResizePanes();
	
	editorSetStatusMessage("%s: i to type, esc then enter to send", E.ai_name);
}

/* hako's look: a turn opens with the keystone and runs down a rail; tools hang
   off it; your lines are marked ›, notices ·. */
static const char *hkReiPrefix(aiData *data, int idx, int sub, Color *pfx, Color *body) {
	unsigned char raw = data->history_role[idx];
	int role = raw & 0x7f, cont = (raw & HK_ROLE_CONT) || sub > 0;
	const char *text = data->history[idx];
	*pfx = E.theme.keyword1;
	*body = E.theme.fg;
	switch (role) {
	case HK_ROLE_USER:
		return cont ? "  " : "\xE2\x80\xBA ";
	case HK_ROLE_AI: {
		if (text && text[0] == '>' && (text[1] == ' ' || text[1] == '\0')) *body = E.theme.comment;
		if (cont) return "\xE2\x94\x82 ";
		int k = idx - 1;
		while (k >= 0 && (data->history_role[k] & 0x7f) == HK_ROLE_SYSTEM && !data->history[k][0]) k--;
		int prev = k >= 0 ? (data->history_role[k] & 0x7f) : -1;
		return (prev == HK_ROLE_AI || prev == HK_ROLE_TOOL || prev == HK_ROLE_TOOLOUT) ? "\xE2\x94\x82 " : "\xE2\x97\x86 ";
	}
	case HK_ROLE_TOOL:
		*body = E.theme.keyword2;
		return cont ? "\xE2\x94\x82 " : "\xE2\x94\x9C ";
	case HK_ROLE_TOOLOUT:
		*body = E.theme.comment;
		return "\xE2\x94\x82 ";
	default:
		*pfx = *body = E.theme.comment;
		if (!text || !*text) return "  ";
		if (!strncmp(text, "Error", 5) || !strncmp(text, "error", 5)) { *pfx = *body = E.theme.err; return cont ? "  " : "! "; }
		return cont ? "  " : "\xC2\xB7 ";
	}
}

/* The commands that match the input line (it starts with : or /): exact name,
   then by prefix, then anywhere in the name. */
static int hkReiMatches(aiData *data, int *out, int max) {
	if (!hk_cmd_n || data->cmd_hide || data->prompt_len < 1) return 0;
	const char *b = data->prompt_buffer;
	if ((b[0] != ':' && b[0] != '/') || strchr(b, ' ') || strchr(b, '\n')) return 0;
	const char *w = b + 1;
	size_t wl = strlen(w);
	int n = 0;
	for (int pass = 0; pass < 3 && n < max; pass++) {
		for (int i = 0; i < hk_cmd_n && n < max; i++) {
			const char *nm = hk_cmds[i].n;
			int hit = pass == 0 ? !strcmp(nm, w) : pass == 1 ? (!strncmp(nm, w, wl) && strcmp(nm, w)) :
			          (wl && strstr(nm, w) && strncmp(nm, w, wl));
			if (hit) out[n++] = i;
		}
	}
	return n;
}

static int hkReiMenuRows(editorPane *pane, int n) {
	int cap = pane->height / 3;
	if (cap > 8) cap = 8;
	if (pane->height < 12) return 0;
	return n < cap ? n : cap;
}

static void hkReiDrawMenu(editorPane *pane, aiData *data, int history_end, int inner_w) {
	if (data->mode != MODE_INSERT) return;
	int m[64];
	int n = hkReiMatches(data, m, 64);
	int rows = hkReiMenuRows(pane, n);
	if (rows <= 0) return;
	if (data->cmd_sel >= n) data->cmd_sel = n - 1;
	if (data->cmd_sel < 0) data->cmd_sel = 0;
	int top = data->cmd_sel >= rows ? data->cmd_sel - rows + 1 : 0;
	int wide = pane->width >= 50;
	for (int r = 0; r < rows; r++) {
		const hkCmd *c = &hk_cmds[m[top + r]];
		int gy = pane->y + history_end + 1 - rows + r;
		int sel = top + r == data->cmd_sel;
		Color fg = sel ? E.theme.visual_fg : E.theme.fg, bg = sel ? E.theme.visual_bg : E.theme.bg;
		Color dim = sel ? fg : E.theme.comment;
		char left[160];
		if (wide) snprintf(left, sizeof(left), " %s%-12s %-10s %s", c->p, c->n, c->a, c->d);
		else snprintf(left, sizeof(left), " %s%s", c->p, c->n);
		int x0 = pane->x + 2;
		for (int x = 0; x < inner_w; x++) gridPutCh(x0 + x, gy, ' ', fg, bg);
		int used = gridPutStr(x0, gy, left, inner_w, fg, bg);
		int cl = (int)strlen(c->c);
		if (wide && used + cl + 2 < inner_w) gridPutStr(x0 + inner_w - cl - 1, gy, c->c, cl, dim, bg);
	}
}

/* "commands":[{"n":..,"p":..,"c":..,"a":..,"d":..}, ...] in hako's init or state */
static void hkReiParseCommands(const char *line) {
	const char *arr = strstr(line, "\"commands\":[");
	if (!arr) return;
	const char *p = arr + 12;
	hkCmd *list = NULL;
	int n = 0;
	while (*p && *p != ']') {
		while (*p == ' ' || *p == ',') p++;
		if (*p != '{') break;
		const char *st = p;
		int depth = 0, instr = 0;
		for (; *p; p++) {
			if (instr) { if (*p == '\\' && p[1]) p++; else if (*p == '"') instr = 0; continue; }
			if (*p == '"') instr = 1;
			else if (*p == '{') depth++;
			else if (*p == '}' && --depth == 0) { p++; break; }
		}
		size_t len = (size_t)(p - st);
		char *obj = malloc(len + 1);
		if (!obj) break;
		memcpy(obj, st, len);
		obj[len] = '\0';
		hkCmd c;
		memset(&c, 0, sizeof(c));
		const char *keys[5] = {"n", "p", "c", "a", "d"};
		char *dst[5] = {c.n, c.p, c.c, c.a, c.d};
		size_t dsz[5] = {sizeof(c.n), sizeof(c.p), sizeof(c.c), sizeof(c.a), sizeof(c.d)};
		for (int k = 0; k < 5; k++) {
			char *raw = hkExtractJsonString(obj, keys[k]);
			char *v = raw ? hkJsonUnescapeDup(raw) : NULL;
			if (v) snprintf(dst[k], dsz[k], "%s", v);
			free(v);
			free(raw);
		}
		free(obj);
		if (!c.n[0]) continue;
		if (!c.p[0]) snprintf(c.p, sizeof(c.p), ":");
		hkCmd *nl = realloc(list, sizeof(hkCmd) * (n + 1));
		if (!nl) break;
		list = nl;
		list[n++] = c;
	}
	free(hk_cmds);
	hk_cmds = list;
	hk_cmd_n = n;
}

void aiRender(editorPane *pane, struct abuf *ab) {
	if (!pane || !pane->ai) return;

	aiData *data = pane->ai;

	pthread_mutex_lock(&data->lock);

	int inner_w = pane->width - 1 - 2;
	if (inner_w < 4) inner_w = 4;
	int seg_w = inner_w - 2;
	if (seg_w < 2) seg_w = 2;

	int *pstart, *pend;
	int plines_total = aiPromptSegs(data, seg_w, &pstart, &pend);
	int plines = plines_total > 5 ? 5 : plines_total;
	int pscroll = plines_total > plines ? plines_total - plines : 0;
	int prompt_rows = 1 + plines + 1;
	int history_end = pane->height - prompt_rows - 1;
	if (history_end < 2) history_end = 2;

	int total_vis = 0;
	int *line_owner = NULL;
	int *line_sub = NULL;
	int owner_cap = 0;
	for (int i = 0; i < data->history_count; i++) {
		int seg_cols_w = inner_w - 2;
		if (seg_cols_w < 1) seg_cols_w = 1;
		int rows = hkReiWrap(data->history[i], seg_cols_w, -1, NULL, NULL, NULL);
		if (rows < 1) rows = 1;
		for (int s = 0; s < rows; s++) {
			if (total_vis >= owner_cap) {
				owner_cap = owner_cap ? owner_cap * 2 : 64;
				line_owner = realloc(line_owner, sizeof(int) * owner_cap);
				line_sub = realloc(line_sub, sizeof(int) * owner_cap);
			}
			line_owner[total_vis] = i;
			line_sub[total_vis] = s;
			total_vis++;
		}
	}
	int spinner_row = -1;
	if (data->streaming) {
		if (total_vis >= owner_cap) {
			owner_cap = owner_cap ? owner_cap * 2 : 64;
			line_owner = realloc(line_owner, sizeof(int) * owner_cap);
			line_sub = realloc(line_sub, sizeof(int) * owner_cap);
		}
		line_owner[total_vis] = -1;
		line_sub[total_vis] = 0;
		spinner_row = total_vis;
		total_vis++;
	}

	int history_rows = history_end - 2 + 1;
	if (history_rows < 0) history_rows = 0;
	int max_scroll = total_vis > history_rows ? total_vis - history_rows : 0;
	if (data->scroll_offset < 0) data->scroll_offset = 0;
	if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
	int scroll = max_scroll - data->scroll_offset;
	if (scroll < 0) scroll = 0;

	for (int y = 0; y < pane->height; y++) {
		int gy = pane->y + y;
		int gx = pane->x;

		gridPutStr(gx, gy, "│", 1, E.theme.border, E.theme.border);

		if (y == 0) {
			Color hf = E.theme.status_fg;
			Color hb = E.theme.status_bg;
			char header[64];
			snprintf(header, sizeof(header), " %s ", E.ai_name);
			int max = pane->width - 1;
			int used = gridPutStr(gx + 1, gy, header, max, hf, hb);
			for (int x = used; x < max; x++) {
				gridPutCh(gx + 1 + x, gy, ' ', hf, hb);
			}
			/* the chat's name, highlighted (no brackets), as hako's bar shows it */
			if (E.hako_session_name[0] && used + 3 < max) {
				char chip[80];
				snprintf(chip, sizeof(chip), " %s ", E.hako_session_name);
				Color cf = E.theme.bg, cb = E.theme.keyword1;
				if (cf.r < 0 || cb.r < 0) { cf = E.theme.keyword1; cf.a |= HK_A_REV; cb = E.theme.bg; }
				used += 1 + gridPutStr(gx + 2 + used, gy, chip, max - used - 1, cf, cb);
			}
			if (E.hako_remote && used + 8 < max) gridPutStr(gx + 2 + used, gy, " remote", max - used - 1, E.theme.string, hb);
		} else if (y == 1 || y == history_end + 1) {
			int used = 0;
			while (used < pane->width - 1) {
				int w = gridPutStr(gx + 1 + used, gy, "─", pane->width - 1 - used, E.theme.border, E.theme.bg);
				if (w <= 0) break;
				used += w;
			}
		} else if (y >= history_end + 2) {
			int prow = y - (history_end + 2);
			int cx = gx + 1;
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
			cx++;
			int vline = prow + pscroll;
			if (prow < plines) {
				Color pfx_fg = (data->mode == MODE_INSERT) ? E.theme.keyword1 : E.theme.comment;
				const char *pfx = (vline == 0) ? (data->mode == MODE_INSERT ? "> " : "  ") : "  ";
				gridPutStr(cx, gy, pfx, 2, pfx_fg, E.theme.bg);
				cx += 2;

				int pw = inner_w - 2;
				int shown = 0;
				if (data->prompt_buffer && data->prompt_len > 0 && vline < plines_total) {
					int len = pend[vline] - pstart[vline];
					if (len > pw) len = pw;
					if (len > 0) {
						char tmp[1024];
						if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
						memcpy(tmp, data->prompt_buffer + pstart[vline], len);
						tmp[len] = '\0';
						shown = gridPutStr(cx, gy, tmp, pw, E.theme.fg, E.theme.bg);
					}
				} else if (vline == 0 && data->mode != MODE_INSERT) {
					const char *hint = "press 'i' to type, Enter to send";
					shown = gridPutStr(cx, gy, hint, pw, E.theme.comment, E.theme.bg);
				}
				for (int x = shown; x < pw; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += pw;
			} else {
				for (int x = 0; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
			}
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
		} else {
			int vrow = (y - 2) + scroll;
			int cx = gx + 1;
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
			cx++;
			if (vrow == spinner_row && spinner_row >= 0) {
				static const char *frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
				data->spinner_frame = (data->spinner_frame + 1) % 10;
				int f = data->spinner_frame;
				int sx = cx;
				sx += gridPutStr(sx, gy, frames[f], 1, E.theme.string, E.theme.bg);
				sx += gridPutStr(sx, gy, " thinking...", 12, E.theme.string, E.theme.bg);
				int filled = sx - cx;
				for (int x = filled; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
				gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
				continue;
			}
			if (vrow >= 0 && vrow < total_vis) {
				int msg_idx = line_owner[vrow];
				int sub = line_sub[vrow];
				char *text = data->history[msg_idx];
				int seg_max = inner_w - 2;
				int byte_off = 0, seg_len = 0, seg_cols = 0;
				hkReiWrap(text, seg_max, sub, &byte_off, &seg_len, &seg_cols);

				int is_selected = (data->visual_mode &&
					msg_idx >= MIN(data->visual_start, data->visual_end) &&
					msg_idx <= MAX(data->visual_start, data->visual_end));
				Color bar_fg, body_fg, row_bg = E.theme.bg;
				const char *bar = hkReiPrefix(data, msg_idx, sub, &bar_fg, &body_fg);
				if (is_selected) {
					bar_fg = body_fg = E.theme.visual_fg;
					row_bg = E.theme.visual_bg;
				}
				gridPutStr(cx, gy, bar, 2, bar_fg, row_bg);
				cx += 2;

				if (seg_len > 0) {
					char tmp[2048];
					int n = seg_len < (int)sizeof(tmp) - 1 ? seg_len : (int)sizeof(tmp) - 1;
					memcpy(tmp, text + byte_off, n);
					tmp[n] = '\0';
					gridPutStr(cx, gy, tmp, seg_max, body_fg, row_bg);
				}
				for (int x = seg_cols; x < seg_max; x++) {
					gridPutCh(cx + x, gy, ' ', body_fg, row_bg);
				}
				cx += seg_max;
			} else {
				for (int x = 0; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
			}
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
		}
	}

	hkReiDrawMenu(pane, data, history_end, inner_w);

	free(line_owner);
	free(line_sub);
	free(pstart);
	free(pend);
	pthread_mutex_unlock(&data->lock);
	(void)ab;
}

/*** hako agent pipe client ***/

/* Persistent subprocess: hako speaks JSONL over stdin/stdout to hako --pipe.
   stdin  (to hako)   ← {"type":"prompt","text":"..."} | {"type":"slash","cmd":"..."} | {"type":"quit"}
   stdout (from hako) → {"type":"init",...} | {"type":"message",...} | {"type":"tool_start/end",...} | {"type":"done",...} */

static int   g_hako_fd_write      = -1;
static FILE *g_hako_fp_read       = NULL;
static pid_t g_hako_pid           = -1;
static pthread_t g_hako_reader;
static int g_hako_reader_running  = 0;
static int g_hako_reader_valid    = 0;
static aiData *g_hako_data        = NULL;

static char *hkExtractJsonString(const char *src, const char *key) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char *p = strstr(src, pat);
	if (!p) return NULL;
	p += strlen(pat);
	/* Walk FORWARD honouring escapes. A lookbehind cannot tell `\"` (escaped
	   quote) from `\\"` (escaped backslash, then the real closing quote), so a
	   value ending in a backslash ran off the end and returned NULL — and the
	   caller reads that as "no such field". Same bug cost hako-code whole
	   chunks of streamed answers; the Rei pane parses the agent's JSON over
	   --pipe, so it had the same exposure. */
	const char *end = p;
	while (*end) {
		if (*end == '\\') { if (!end[1]) break; end += 2; continue; }
		if (*end == '"') break;
		end++;
	}
	if (*end != '"') return NULL;
	int len = end - p;
	char *out = malloc(len + 1);
	memcpy(out, p, len);
	out[len] = '\0';
	return out;
}

/* hkExtractJsonString hands back the RAW slice between the quotes, still
   JSON-escaped. Everything that shows it to a human has to undo that first —
   without this the Rei pane printed a literal \n in the middle of the agent's
   sentences instead of breaking the line. */
static char *hkJsonUnescapeDup(const char *s) {
	if (!s) return NULL;
	size_t n = strlen(s);
	char *out = malloc(n + 1);
	if (!out) return NULL;
	size_t o = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] != '\\' || i + 1 >= n) { out[o++] = s[i]; continue; }
		switch (s[++i]) {
		case 'n':  out[o++] = '\n'; break;
		case 't':  out[o++] = '\t'; break;
		case 'r':  out[o++] = '\r'; break;
		case 'b':  out[o++] = '\b'; break;
		case 'f':  out[o++] = '\f'; break;
		case '"':  out[o++] = '"';  break;
		case '/':  out[o++] = '/';  break;
		case '\\': out[o++] = '\\'; break;
		case 'u': {
			unsigned cp = 0; int ok = (i + 4 < n);
			for (int k = 1; ok && k <= 4; k++) {
				char c = s[i + k]; cp <<= 4;
				if      (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
				else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
				else ok = 0;
			}
			if (!ok) { out[o++] = '\\'; out[o++] = 'u'; break; }
			i += 4;
			if (cp < 0x80) out[o++] = (char)cp;
			else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
			else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
			break;
		}
		default: out[o++] = s[i]; break;
		}
	}
	out[o] = '\0';
	return out;
}

static int hkExtractJsonInt(const char *src, const char *key) {
	if (!src) return -1;
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char *p = strstr(src, pat);
	if (!p) return -1;
	p += strlen(pat);
	while (*p == ' ') p++;
	if (*p < '0' || *p > '9') return -1;
	return atoi(p);
}

static void hakoJsonEsc(const char *src, char *dst, int dsz) {
	int d = 0;
	for (const char *p = src; *p && d < dsz - 2; p++) {
		unsigned char c = (unsigned char)*p;
		if      (c == '"')  { if (d < dsz-3) { dst[d++]='\\'; dst[d++]='"';  } }
		else if (c == '\\') { if (d < dsz-3) { dst[d++]='\\'; dst[d++]='\\'; } }
		else if (c == '\n') { if (d < dsz-3) { dst[d++]='\\'; dst[d++]='n';  } }
		else if (c == '\r') { if (d < dsz-3) { dst[d++]='\\'; dst[d++]='r';  } }
		else if (c == '\t') { if (d < dsz-3) { dst[d++]='\\'; dst[d++]='t';  } }
		else                { dst[d++] = (char)c; }
	}
	dst[d] = '\0';
}

static void hakoSendLine(const char *json) {
	if (g_hako_fd_write < 0) return;
	size_t len = strlen(json);
	ssize_t w1 = write(g_hako_fd_write, json, len);
	ssize_t w2 = write(g_hako_fd_write, "\n", 1);
	if (w1 < 0 || w2 < 0) {
		close(g_hako_fd_write);
		g_hako_fd_write = -1;
	}
}


void aiAddHistory(aiData *data, const char *text);

static void aiPushHistoryLine(aiData *data, const char *text, int len, unsigned char role) {
	if (data->history_count >= AI_HISTORY_MAX) return;
	char *line = malloc(len + 1);
	if (!line) return;
	memcpy(line, text, len);
	line[len] = '\0';
	data->history_role[data->history_count] = role;
	data->history[data->history_count++] = line;
}

static void aiAddHistoryLine(aiData *data, const char *text, int text_len, unsigned char role) {
	int max_width = 60;
	if (text_len <= 0) { aiPushHistoryLine(data, "", 0, role); return; }
	if (text_len <= max_width) { aiPushHistoryLine(data, text, text_len, role); return; }
	int pos = 0;
	while (pos < text_len) {
		if (data->history_count >= AI_HISTORY_MAX) break;
		int end = pos + max_width;
		unsigned char r = pos ? (role | HK_ROLE_CONT) : role;
		if (end >= text_len) { aiPushHistoryLine(data, text + pos, text_len - pos, r); break; }
		int wrap = end;
		while (wrap > pos && text[wrap] != ' ') wrap--;
		if (wrap == pos) wrap = end;
		aiPushHistoryLine(data, text + pos, wrap - pos, r);
		pos = wrap;
		while (pos < text_len && text[pos] == ' ') pos++;
	}
}

void aiAddHistoryRole(aiData *data, const char *text, unsigned char role) {
	if (!data || !text) return;
	if (data->history_count > 0 && data->history_count < AI_HISTORY_MAX - 1) {
		unsigned char prev = data->history_role[data->history_count - 1] & 0x7f;
		int role_swap = (prev != role) && role != HK_ROLE_TOOL && role != HK_ROLE_TOOLOUT &&
		    prev != HK_ROLE_TOOL && prev != HK_ROLE_TOOLOUT &&
		    (prev == HK_ROLE_USER || prev == HK_ROLE_AI ||
		     role == HK_ROLE_USER || role == HK_ROLE_AI);
		char *prev_text = data->history[data->history_count - 1];
		if (role_swap && prev_text && *prev_text != '\0')
			aiPushHistoryLine(data, "", 0, HK_ROLE_SYSTEM);
	}
	const char *p = text;
	unsigned char r = role;
	while (1) {
		const char *nl = strchr(p, '\n');
		int seg = nl ? (int)(nl - p) : (int)strlen(p);
		aiAddHistoryLine(data, p, seg, r);
		if (!nl) break;
		p = nl + 1;
		r = role | HK_ROLE_CONT;
	}
}

void aiAddHistory(aiData *data, const char *text) {
	aiAddHistoryRole(data, text, HK_ROLE_SYSTEM);
}

/* session_name and remote, from init or state (absent on an older hako) */
static void hkReiSessionFields(const char *line) {
	char *nm = hkExtractJsonString(line, "session_name");
	if (nm) {
		char *u = hkJsonUnescapeDup(nm);
		snprintf(E.hako_session_name, sizeof(E.hako_session_name), "%s", u ? u : nm);
		free(u);
		free(nm);
	}
	const char *r = strstr(line, "\"remote\":");
	if (r) E.hako_remote = !strncmp(r + 9, "true", 4) || r[9] == '1';
}

static void hakoHandleEvent(aiData *data, const char *line) {
	char *type = hkExtractJsonString(line, "type");
	if (!type) return;

	if (strcmp(type, "init") == 0) {
		char *session  = hkExtractJsonString(line, "session");
		int   resumed  = hkExtractJsonInt(line, "resumed");
		int   turns    = hkExtractJsonInt(line, "turns");
		char *provider = hkExtractJsonString(line, "provider");
		char *model    = hkExtractJsonString(line, "model");
		char *version  = hkExtractJsonString(line, "version");
		int   protocol = hkExtractJsonInt(line, "protocol");

		free(E.hako_session_id);
		E.hako_session_id      = session ? session : strdup("");
		E.hako_session_turns   = turns > 0 ? turns : 0;
		E.hako_session_resumed = resumed > 0 ? 1 : 0;
		E.hako_protocol        = protocol > 0 ? protocol : 1;

		hkReiParseCommands(line);
		hkReiSessionFields(line);
		pthread_mutex_lock(&data->lock);
		char info[256];
		snprintf(info, sizeof(info), "hako %s%s%s%s%s%s",
			version ? version : "(protocol 1: tool writes need :auto)",
			(provider && *provider) ? " \xC2\xB7 " : "", provider ? provider : "",
			(model && *model) ? " \xC2\xB7 " : "", model ? model : "",
			E.hako_session_resumed && E.hako_session_turns > 0 ? " \xC2\xB7 resumed" : "");
		aiAddHistory(data, info);
		pthread_mutex_unlock(&data->lock);

		free(provider);
		free(model);
		free(version);

	} else if (strcmp(type, "state") == 0) {
		/* A menu pick in hako says nothing in the chat; it lands here, in the status line. */
		char *provider = hkExtractJsonString(line, "provider");
		char *model    = hkExtractJsonString(line, "model");
		hkReiParseCommands(line);
		hkReiSessionFields(line);
		if (provider || model)
			editorSetStatusMessage("Rei: %s%s%s", provider ? provider : "",
				(model && *model) ? " · " : "", model ? model : "");
		free(provider);
		free(model);

	} else if (strcmp(type, "message") == 0) {
		char *role = hkExtractJsonString(line, "role");
		char *text = hkExtractJsonString(line, "text");
		if (text) {
			unsigned char hrole = HK_ROLE_SYSTEM;
			if (role && strcmp(role, "ai")   == 0) hrole = HK_ROLE_AI;
			if (role && strcmp(role, "user") == 0) hrole = HK_ROLE_USER;
			char *shown = hkJsonUnescapeDup(text);
			pthread_mutex_lock(&data->lock);
			aiAddHistoryRole(data, shown ? shown : text, hrole);
			pthread_mutex_unlock(&data->lock);
			free(shown);
			free(text);
		}
		free(role);

	} else if (strcmp(type, "tool_start") == 0 || strcmp(type, "tool_end") == 0) {
		char *display = hkExtractJsonString(line, "display");
		if (display) {
			char *u = hkJsonUnescapeDup(display);
			char row[600];
			int start = type[5] == 's';
			snprintf(row, sizeof(row), "%s%.560s", start ? "" : "  ", u ? u : display);
			pthread_mutex_lock(&data->lock);
			aiAddHistoryRole(data, row, start ? HK_ROLE_TOOL : HK_ROLE_TOOLOUT);
			pthread_mutex_unlock(&data->lock);
			free(u);
			free(display);
		}

	} else if (strcmp(type, "tool_result") == 0) {
		/* The agent sends the tool's actual OUTPUT here. The pane used to drop
		   it and show only the call line from tool_start/tool_end, so the Rei
		   pane said "ran list_dir" and never what it found — the CLI shows
		   both. Clipped, because a long result would bury the conversation;
		   the full text is in the agent's own transcript. */
		char *text = hkExtractJsonString(line, "text");
		if (text) {
			char *u = hkJsonUnescapeDup(text);
			const char *body = u ? u : text;
			int shown = 0;
			const char *q = body;
			while (*q && shown < HAKE_TOOL_RESULT_LINES) {
				const char *nl = strchr(q, '\n');
				int n = nl ? (int)(nl - q) : (int)strlen(q);
				if (n > 0) {
					char row[256];
					snprintf(row, sizeof(row), "  %.*s", n < 240 ? n : 240, q);
					pthread_mutex_lock(&data->lock);
					aiAddHistoryRole(data, row, HK_ROLE_TOOLOUT);
					pthread_mutex_unlock(&data->lock);
					shown++;
				}
				if (!nl) break;
				q = nl + 1;
			}
			if (*q) {
				pthread_mutex_lock(&data->lock);
				aiAddHistoryRole(data, "  …", HK_ROLE_TOOLOUT);
				pthread_mutex_unlock(&data->lock);
			}
			free(u); free(text);
		}

	} else if (strcmp(type, "done") == 0) {
		int turn    = hkExtractJsonInt(line, "turn");
		char *session = hkExtractJsonString(line, "session");

		free(E.hako_session_id);
		E.hako_session_id    = session ? session : strdup("");
		E.hako_session_turns = turn > 0 ? turn : E.hako_session_turns;

		pthread_mutex_lock(&data->lock);
		data->streaming = 0;
		pthread_mutex_unlock(&data->lock);

	} else if (strcmp(type, "error") == 0) {
		char *msg = hkExtractJsonString(line, "message");
		pthread_mutex_lock(&data->lock);
		aiAddHistory(data, msg ? msg : "Error: unknown");
		data->streaming = 0;
		pthread_mutex_unlock(&data->lock);
		free(msg);

	} else if (strcmp(type, "choice_request") == 0) {
		/* :model / :provider / :sessions in the pane. hako cannot draw a tty
		   popup over --pipe, so it sends the choices here and blocks; we render
		   the same popup the editor uses and answer with the index. */
		long cid = hkExtractJsonInt(line, "id");
		char *q  = hkExtractJsonString(line, "question");
		/* parse "options":[ "a", "b", ... ] honouring escapes */
		char *opts[128]; int nopt = 0;
		const char *arr = strstr(line, "\"options\":[");
		if (arr) {
			const char *p2 = arr + 11;   /* past the '[' */
			while (*p2 && *p2 != ']' && nopt < 128) {
				while (*p2 == ' ' || *p2 == ',' ) p2++;
				if (*p2 != '"') break;
				p2++;
				const char *st = p2;
				while (*p2) { if (*p2 == '\\') { if (!p2[1]) break; p2 += 2; continue; } if (*p2 == '"') break; p2++; }
				size_t len = (size_t)(p2 - st);
				char *raw = malloc(len + 1);
				if (raw) { memcpy(raw, st, len); raw[len] = '\0';
				           opts[nopt] = hkJsonUnescapeDup(raw); if (!opts[nopt]) opts[nopt] = raw; else free(raw);
				           nopt++; }
				if (*p2 == '"') p2++;
			}
		}
		if (nopt > 0) {
			char qtitle[64];
			char *qd = q ? hkJsonUnescapeDup(q) : NULL;
			snprintf(qtitle, sizeof(qtitle), "%s", qd ? qd : "choose");
			free(qd);
			editorPopupOpen(qtitle, (const char *const *)opts, nopt, 1, 0);
			E.popup_choice_id = cid;
			for (int i = 0; i < nopt; i++) free(opts[i]);
		} else {
			/* nothing to pick: unblock hako */
			char m[64]; snprintf(m, sizeof(m), "{\"type\":\"choice\",\"id\":%ld,\"answer\":\"\"}", cid);
			hakoSendLine(m);
		}
		free(q);

	} else if (strcmp(type, "tool_request") == 0) {
		int   id      = hkExtractJsonInt(line, "id");
		char *tool    = hkExtractJsonString(line, "name");
		if (!tool) tool = hkExtractJsonString(line, "tool");
		char *kind    = hkExtractJsonString(line, "kind");
		char *subject = hkExtractJsonString(line, "subject");
		char *diff    = hkExtractJsonString(line, "diff");
		pthread_mutex_lock(&data->lock);
		data->perm_pending = 1;
		data->perm_id      = id;
		snprintf(data->perm_tool,    sizeof(data->perm_tool),    "%s", tool    ? tool    : "tool");
		snprintf(data->perm_kind,    sizeof(data->perm_kind),    "%s", kind    ? kind    : "read");
		snprintf(data->perm_subject, sizeof(data->perm_subject), "%s", subject ? subject : "");
		char ask[1280];
		snprintf(ask, sizeof(ask), "\xE2\x97\x8F %s  %s", data->perm_tool,
			*data->perm_subject ? data->perm_subject : "(no subject)");
		aiAddHistory(data, ask);
		/* The agent sends a preview of the change for write tools. Approving a
		   file rewrite you cannot see is not consent. */
		if (diff) {
			char row[1024];
			size_t w = 0;
			for (const char *d = diff; ; d++) {
				int nl = (*d == '\0') || (*d == '\\' && d[1] == 'n');
				if (nl || w + 2 >= sizeof(row)) {
					row[w] = '\0';
					if (w) aiAddHistory(data, row);
					w = 0;
					if (*d == '\0') break;
					if (nl) { d++; continue; }
				}
				if (*d == '\\' && d[1]) {
					d++;
					row[w++] = (*d == 't') ? '\t' : (*d == 'r') ? ' ' : *d;
				} else {
					row[w++] = *d;
				}
			}
		}
		const char *always =
			strcmp(data->perm_kind, "write") == 0 ? "always write this project" :
			strcmp(data->perm_kind, "shell") == 0 ? "always run exactly this"   :
			strcmp(data->perm_kind, "net")   == 0 ? "always fetch from the web" :
			                                        "always read this project";
		snprintf(ask, sizeof(ask), "allow?  [y] once   [n] no   [a] %s", always);
		aiAddHistory(data, ask);
		pthread_mutex_unlock(&data->lock);
		free(tool); free(kind); free(subject); free(diff);
	}

	free(type);
}

static void *hakoReaderThread(void *arg) {
	aiData *data = (aiData *)arg;
	/* Whole lines, however long: a fixed buffer split a big tool_result or diff
	   in two, and the first half parsed as JSON with its fields missing. */
	char *line = NULL;
	size_t lcap = 0;
	ssize_t got;
	while (g_hako_fp_read && (got = getline(&line, &lcap, g_hako_fp_read)) != -1) {
		ssize_t len = got;
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
		if (len == 0) continue;
		hakoHandleEvent(data, line);
		/* No full_redraw_pending here: it cleared the whole screen (\x1b[2J) for every
		   event, so the editor flashed several times per reply. The main loop wakes
		   every 100 ms (VTIME) and its grid diff repaints only what changed. */
	}
	free(line);
	pthread_mutex_lock(&data->lock);
	data->streaming = 0;
	pthread_mutex_unlock(&data->lock);
	g_hako_reader_running = 0;
	return NULL;
}

static char *hakoTryDir(const char *dir) {
	static const char *names[] = { "hako", "hako-bundled" };
	for (int i = 0; i < 2; i++) {
		char cand[PATH_MAX];
		snprintf(cand, sizeof(cand), "%s/%s", dir, names[i]);
		if (access(cand, X_OK) == 0) return strdup(cand);
	}
	return NULL;
}

static char *hakoFindBinary(void) {
	char buf[PATH_MAX];
	char *found;

#ifdef __APPLE__
	{
		uint32_t sz = sizeof(buf);
		if (_NSGetExecutablePath(buf, &sz) == 0) {
			char real[PATH_MAX];
			if (realpath(buf, real)) {
				char *slash = strrchr(real, '/');
				if (slash) { *slash = '\0'; if ((found = hakoTryDir(real))) return found; }
			}
		}
	}
#elif defined(__linux__)
	{
		ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			char *slash = strrchr(buf, '/');
			if (slash) { *slash = '\0'; if ((found = hakoTryDir(buf))) return found; }
		}
	}
#endif

	if ((found = hakoTryDir("."))) return found;

	const char *home = getenv("HOME");
	if (home) {
		snprintf(buf, sizeof(buf), "%s/.local/bin", home);
		if ((found = hakoTryDir(buf))) return found;
	}

	if ((found = hakoTryDir("/usr/local/bin"))) return found;

	const char *path = getenv("PATH");
	if (path) {
		const char *p = path;
		while (*p) {
			const char *colon = strchr(p, ':');
			int seg = colon ? (int)(colon - p) : (int)strlen(p);
			if (seg > 0 && seg < PATH_MAX - 6) {
				char dir[PATH_MAX];
				memcpy(dir, p, seg);
				dir[seg] = '\0';
				if ((found = hakoTryDir(dir))) return found;
			}
			if (!colon) break;
			p = colon + 1;
		}
	}
	return NULL;
}

static int hakoLaunch(aiData *data) {
#ifdef HAKE_NO_AGENT
	(void)data; (void)hakoReaderThread; (void)g_hako_reader;
	(void)hakoFindBinary; (void)g_hako_data; (void)g_hako_reader_valid; (void)g_hako_pid;
	return -3;
#elif defined(_WIN32)
	(void)data; (void)hakoReaderThread; (void)g_hako_reader;
	(void)hakoFindBinary; (void)g_hako_data; (void)g_hako_reader_valid; (void)g_hako_pid;
	return -2;
#else
	char *bin = hakoFindBinary();
	if (!bin) return -2;
	int to_child[2], from_child[2];
	if (pipe(to_child)   < 0 ||
	    pipe(from_child) < 0) { free(bin); return -1; }
	pid_t pid = fork();
	if (pid < 0) {
		close(to_child[0]);   close(to_child[1]);
		close(from_child[0]); close(from_child[1]);
		free(bin); return -1;
	}
	if (pid == 0) {
		dup2(to_child[0],   STDIN_FILENO);
		dup2(from_child[1], STDOUT_FILENO);
		close(to_child[0]);   close(to_child[1]);
		close(from_child[0]); close(from_child[1]);
		char *argv[] = {bin, "--pipe", NULL};
		execvp(bin, argv);
		_exit(127);
	}
	close(to_child[0]);
	close(from_child[1]);
	free(bin);
	g_hako_fd_write     = to_child[1];
	g_hako_fp_read      = fdopen(from_child[0], "r");
	g_hako_pid          = pid;
	g_hako_data         = data;
	g_hako_reader_running = 1;
	if (pthread_create(&g_hako_reader, NULL, hakoReaderThread, data) != 0) {
		fclose(g_hako_fp_read); g_hako_fp_read  = NULL;
		close(g_hako_fd_write); g_hako_fd_write = -1;
		g_hako_pid            = -1;
		g_hako_reader_running = 0;
		return -1;
	}
	g_hako_reader_valid = 1;
	return 0;
#endif
}

static void hakoShutdown(void) {
#ifndef _WIN32
	/* order matters: EOF child + reap, join reader, then fclose — reader touches aiData */
	if (g_hako_fd_write >= 0) {
		const char *quit = "{\"type\":\"quit\"}\n";
		ssize_t w = write(g_hako_fd_write, quit, strlen(quit));
		(void)w;
		close(g_hako_fd_write);
		g_hako_fd_write = -1;
	}
	if (g_hako_pid > 0) {
		for (int i = 0; i < 20; i++) {
			pid_t r = waitpid(g_hako_pid, NULL, WNOHANG);
			if (r == g_hako_pid || r < 0) break;
			usleep(10000);
		}
		if (waitpid(g_hako_pid, NULL, WNOHANG) == 0) {
			kill(g_hako_pid, SIGTERM);
			waitpid(g_hako_pid, NULL, 0);
		}
		g_hako_pid = -1;
	}
	if (g_hako_reader_valid) {
		pthread_join(g_hako_reader, NULL);
		g_hako_reader_valid = 0;
	}
	if (g_hako_fp_read) {
		fclose(g_hako_fp_read);
		g_hako_fp_read = NULL;
	}
	g_hako_reader_running = 0;
	g_hako_data           = NULL;
#endif
}

static void hakoInstallOrUpdate(aiData *data, int update) {
#ifdef _WIN32
	(void)update;                 /* only the POSIX branch below distinguishes them */
	pthread_mutex_lock(&data->lock);
	aiAddHistory(data, "Agent install/update is not wired on Windows yet.");
	aiAddHistory(data, "Install manually from https://mithraeums.github.io/hako.sh");
	pthread_mutex_unlock(&data->lock);
	return;
#else
	char cmd[PATH_MAX + 32];
	char *bin = update ? hakoFindBinary() : NULL;
	if (update && bin) snprintf(cmd, sizeof(cmd), "%s --update 2>&1", bin);
	else snprintf(cmd, sizeof(cmd),
		"curl -fsSL https://mithraeums.github.io/hako.sh | sh 2>&1");
	free(bin);

	pthread_mutex_lock(&data->lock);
	aiAddHistory(data, update ? "updating agent..." : "installing agent...");
	pthread_mutex_unlock(&data->lock);
	editorRefreshScreen();

	FILE *fp = popen(cmd, "r");
	if (!fp) {
		pthread_mutex_lock(&data->lock);
		aiAddHistory(data, "could not start installer");
		pthread_mutex_unlock(&data->lock);
		return;
	}
	char ln[512], last[512] = "";
	while (fgets(ln, sizeof(ln), fp)) {
		ln[strcspn(ln, "\r\n")] = '\0';
		if (*ln) snprintf(last, sizeof(last), "%s", ln);
	}
	int rc = pclose(fp);

	pthread_mutex_lock(&data->lock);
	if (*last) aiAddHistory(data, last);
	pthread_mutex_unlock(&data->lock);

	if (rc != 0) {
		pthread_mutex_lock(&data->lock);
		aiAddHistory(data, "install/update failed — see output above");
		pthread_mutex_unlock(&data->lock);
		return;
	}
	hakoShutdown();
	int r = hakoLaunch(data);
	pthread_mutex_lock(&data->lock);
	aiAddHistory(data, r == 0 ? "agent connected." : "agent still not found after install.");
	pthread_mutex_unlock(&data->lock);
#endif
}

void aiWorkerSend(aiData *data) {
	if (!data) return;
	if (data->streaming) return;
	if (g_hako_fd_write < 0 && hakoLaunch(data) != 0) {
		pthread_mutex_lock(&data->lock);
		aiAddHistory(data, "Agent not connected. :install-agent, or :q rei then :rei.");
		pthread_mutex_unlock(&data->lock);
		return;
	}
	data->streaming = 1;
	char esc[8192];
	hakoJsonEsc(data->current_prompt ? data->current_prompt : "", esc, sizeof(esc));
	char json[8256];
	snprintf(json, sizeof(json), "{\"type\":\"prompt\",\"text\":\"%s\"}", esc);
	hakoSendLine(json);
}

/* :voice and friends typed at the editor go to hako, which owns them */
static void hkAgentSlash(const char *cmd) {
	if (!E.right_panel || E.right_panel->type != PANE_AI || !E.right_panel->ai) {
		editorSetStatusMessage(":%s is hako's: open the agent with :rei first", cmd);
		return;
	}
	char line[300];
	snprintf(line, sizeof(line), ":%s", cmd);
	hkHandleSlash(E.right_panel->ai, line);
}

int hkHandleSlash(aiData *data, const char *prompt) {
	if (!data) return 0;
	if (!strcmp(prompt, "/quit") || !strcmp(prompt, "/q") || !strcmp(prompt, ":quit") || !strcmp(prompt, ":q")) return 2;
	if (g_hako_fd_write < 0) {
		pthread_mutex_lock(&data->lock);
		aiAddHistory(data, "Rei not connected.");
		pthread_mutex_unlock(&data->lock);
		return 0;
	}
	char esc[4096];
	hakoJsonEsc(prompt, esc, sizeof(esc));
	char json[4160];
	snprintf(json, sizeof(json), "{\"type\":\"slash\",\"cmd\":\"%s\"}", esc);
	data->streaming = 1;
	hakoSendLine(json);
	return 0;
}

/* :config opens the rc (a short commented one is written when there is none);
   :config key=value sets one key in place and applies it now. */
static const char *HK_RC_TEMPLATE =
	"# ~/.hakorc: one file for hake, hako and hako studio (key=value, # comments)\n"
	"# a key prefixed edit., code. or studio. is that tool's and beats the plain one\n"
	"# a project's own .hakorc is read on top of this one\n"
	"\n"
	"# theme=terminal          # the profile's own colors; or mithraeum, washi, gruvbox, nord ...\n"
	"# theme.mine.base=nord    # a custom theme: theme.<name>.<slot>=#rrggbb | ansiN | default\n"
	"# theme.mine.comment=#7a7a7a\n"
	"\n"
	"# edit.keys=vim           # vim, emacs or nano\n"
	"# show_line_numbers=0\n"
	"# status_line=1           # 0 never, 1 with more than one pane, 2 always\n"
	"# word_wrap=1\n"
	"# tab_stop=8\n"
	"# use_tabs=1\n"
	"# mouse=1\n"
	"# build_cmd=make          # :build\n"
	"# test_cmd=make test      # :test\n"
	"# run_cmd=./a.out         # :run\n"
	"# console_sandbox=1       # :console runs under sandbox-exec or bwrap (global rc only)\n";

void editorGenerateConfig(void) {
	if (!E.config_path) { editorSetStatusMessage("no HOME: nowhere to keep a config"); return; }
	if (!hkFileExists(E.config_path)) {
		FILE *fp = fopen(E.config_path, "w");
		if (!fp) { editorSetStatusMessage("cannot create %s: %s", E.config_path, strerror(errno)); return; }
		fputs(HK_RC_TEMPLATE, fp);
		fclose(fp);
	}
	hkOpenPath(E.config_path);
}

static void hkConfigSet(const char *kv, int persist) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s", kv);
	char *key, *val;
	if (!hkRcSplit(buf, &key, &val)) { editorSetStatusMessage("usage: key=value"); return; }
	const char *k = strncmp(key, "edit.", 5) == 0 ? key + 5 : key;
	int known = hkApplyKey(k, val);
	E.full_redraw_pending = 0;
	if (!persist) {
		editorSetStatusMessage(known ? "%s=%s" : "%s: not a hake setting", k, val);
		return;
	}
	if (hkRcSet(key, val)) editorSetStatusMessage("%s=%s (saved to %s)%s", key, val, hkBaseName(E.config_path),
	                                              known ? "" : ", not a hake setting");
	else editorSetStatusMessage("could not write %s", E.config_path);
}

/*** init ***/
/* rgb per slot, in Theme's order (tools/themes.py reads these rows) */
static const struct { const char *name; unsigned char c[12][3]; } HK_THEMES[] = {
	{"mithraeum",    {{13,12,10},   {230,224,209}, {138,132,115}, {183,189,202}, {200,186,197}, {220,207,174}, {95,90,78},    {230,224,209}, {13,12,10},    {74,70,61},    {58,54,46},   {242,236,221}}},
	{"washi",        {{236,231,218},{20,19,15},    {110,104,90}, {47,58,79},    {90,70,54},   {92,82,48},    {163,155,136}, {20,19,15},    {236,231,218}, {110,104,90},  {214,206,186},{20,19,15}}},
	{"dark",         {{30,30,30},   {212,212,212}, {106,153,85},  {86,156,214},  {78,201,176}, {206,145,120}, {133,133,133}, {50,50,50},    {212,212,212}, {80,80,80},    {38,79,120},  {255,255,255}}},
	{"light",        {{255,255,255},{30,30,30},    {0,128,0},     {0,0,200},     {0,128,128},  {163,21,21},   {150,150,150}, {220,220,220}, {30,30,30},    {180,180,180}, {173,214,255},{0,0,0}}},
	{"gruvbox",      {{40,40,40},   {235,219,178}, {146,131,116}, {251,73,52},   {184,187,38}, {184,187,38},  {124,111,100}, {80,73,69},    {235,219,178}, {80,73,69},    {68,65,59},   {235,219,178}}},
	{"nord",         {{46,52,64},   {216,222,233}, {76,86,106},   {129,161,193}, {136,192,208},{163,190,140}, {76,86,106},   {59,66,82},    {216,222,233}, {59,66,82},    {67,76,94},   {216,222,233}}},
	{"dracula",      {{40,42,54},   {248,248,242}, {98,114,164},  {255,121,198}, {139,233,253},{241,250,140}, {98,114,164},  {68,71,90},    {248,248,242}, {68,71,90},    {68,71,90},   {248,248,242}}},
	{"monokai",      {{39,40,34},   {248,248,242}, {117,113,94},  {249,38,114},  {102,217,239},{230,219,116}, {117,113,94},  {60,61,55},    {248,248,242}, {60,61,55},    {73,72,62},   {248,248,242}}},
	{"solarized",    {{0,43,54},    {131,148,150}, {88,110,117},  {38,139,210},  {42,161,152}, {42,161,152},  {88,110,117},  {7,54,66},     {147,161,161}, {7,54,66},     {7,54,66},    {147,161,161}}},
	{"tokyonight",   {{26,27,38},   {192,202,245}, {86,95,137},   {187,154,247}, {125,207,255},{158,206,106}, {60,68,102},   {36,40,59},    {192,202,245}, {60,68,102},   {40,52,87},   {192,202,245}}},
	{"catppuccin",   {{30,30,46},   {205,214,244}, {108,112,134}, {203,166,247}, {137,220,235},{166,227,161}, {88,91,112},   {49,50,68},    {205,214,244}, {69,71,90},    {69,71,90},   {205,214,244}}},
	{"onedark",      {{40,44,52},   {171,178,191}, {92,99,112},   {198,120,221}, {86,182,194}, {152,195,121}, {76,82,99},    {33,37,43},    {171,178,191}, {60,64,72},    {62,68,81},   {220,223,228}}},
	{"material",     {{38,50,56},   {238,255,255}, {84,110,122},  {199,146,234}, {130,170,255},{195,232,141}, {63,81,88},    {32,43,48},    {238,255,255}, {63,81,88},    {51,71,79},   {238,255,255}}},
	{"everforest",   {{45,53,59},   {211,198,170}, {133,146,137}, {230,126,128}, {127,187,179},{167,192,128}, {86,99,98},    {55,63,68},    {211,198,170}, {67,76,81},    {67,76,81},   {211,198,170}}},
	{"rosepine",     {{25,23,36},   {224,222,244}, {110,106,134}, {196,167,231}, {156,207,216},{246,193,119}, {82,79,103},   {38,35,58},    {224,222,244}, {49,46,73},    {64,61,82},   {224,222,244}}},
	{"github-dark",  {{13,17,23},   {201,209,217}, {139,148,158}, {255,123,114}, {121,192,255},{165,214,255}, {72,79,88},    {22,27,34},    {201,209,217}, {48,54,61},    {33,38,45},   {201,209,217}}},
	{"github-light", {{255,255,255},{36,41,47},    {106,115,125}, {207,34,46},   {5,80,174},   {10,48,105},   {140,149,159}, {240,246,252}, {36,41,47},    {208,215,222}, {221,244,255},{36,41,47}}},
	{"ayu",          {{15,20,25},   {191,189,182}, {92,103,115},  {255,143,64},  {57,186,230}, {170,217,76},  {60,75,90},    {20,25,31},    {191,189,182}, {38,48,59},    {38,48,59},   {191,189,182}}},
	{"kanagawa",     {{31,31,40},   {220,215,186}, {114,113,105}, {149,127,184}, {127,180,202},{152,187,108}, {84,84,109},   {42,42,55},    {220,215,186}, {54,54,70},    {43,57,63},   {220,215,186}}},
};

/* terminal: nothing of ours. The profile's default fg/bg, never a painted
   background, and syntax in its own 16 colors (the same ones hako uses). */
static const Theme HK_THEME_TERMINAL = {
	{HK_COL_DEFAULT, 0, 0, 0},              /* bg */
	{HK_COL_DEFAULT, 0, 0, 0},              /* fg */
	{HK_COL_ANSI, 8, 0, 0},                 /* comment */
	{HK_COL_ANSI, 3, 0, 0},                 /* keyword */
	{HK_COL_ANSI, 6, 0, 0},                 /* type */
	{HK_COL_ANSI, 2, 0, 0},                 /* string */
	{HK_COL_ANSI, 8, 0, 0},                 /* line_number */
	{HK_COL_DEFAULT, 0, 0, HK_A_REV},       /* status_bg: reverse video, like vim */
	{HK_COL_DEFAULT, 0, 0, 0},              /* status_fg */
	{HK_COL_ANSI, 8, 0, 0},                 /* border */
	{HK_COL_DEFAULT, 0, 0, HK_A_REV},       /* visual_bg */
	{HK_COL_DEFAULT, 0, 0, 0},              /* visual_fg */
	{HK_COL_ANSI, 5, 0, 0},                 /* number */
	{HK_COL_ANSI, 1, 0, 0},                 /* err */
};

/* Slot names a custom theme may set: the suite's shared names, plus hake's own. */
static const struct { const char *name; size_t off; } HK_SLOTS[] = {
	{"bg", offsetof(Theme, bg)},               {"fg", offsetof(Theme, fg)},
	{"comment", offsetof(Theme, comment)},     {"keyword", offsetof(Theme, keyword1)},
	{"keyword1", offsetof(Theme, keyword1)},   {"type", offsetof(Theme, keyword2)},
	{"keyword2", offsetof(Theme, keyword2)},   {"string", offsetof(Theme, string)},
	{"number", offsetof(Theme, number)},       {"line_number", offsetof(Theme, line_number)},
	{"status_bg", offsetof(Theme, status_bg)}, {"status_fg", offsetof(Theme, status_fg)},
	{"border", offsetof(Theme, border)},       {"accent", offsetof(Theme, border)},
	{"visual_bg", offsetof(Theme, visual_bg)}, {"visual_fg", offsetof(Theme, visual_fg)},
	{"err", offsetof(Theme, err)},
};
#define HK_SLOT_COUNT ((int)(sizeof(HK_SLOTS) / sizeof(HK_SLOTS[0])))

static Color *hkSlot(Theme *t, const char *slot) {
	for (int i = 0; i < HK_SLOT_COUNT; i++)
		if (strcmp(slot, HK_SLOTS[i].name) == 0) return (Color *)((char *)t + HK_SLOTS[i].off);
	return NULL;
}

/* #rrggbb, #rgb, ansiN (0-15), default, or the old R,G,B. */
static int hkParseColor(const char *v, Color *out) {
	Color c = {0, 0, 0, 0};
	while (*v == ' ') v++;
	if (v[0] == '#') {
		unsigned x = 0; int n = 0;
		for (const char *p = v + 1; isxdigit((unsigned char)*p); p++, n++)
			x = x * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
		if (n == 6) { c.r = (x >> 16) & 255; c.g = (x >> 8) & 255; c.b = x & 255; }
		else if (n == 3) { c.r = ((x >> 8) & 15) * 17; c.g = ((x >> 4) & 15) * 17; c.b = (x & 15) * 17; }
		else return 0;
	} else if (strncmp(v, "ansi", 4) == 0 && isdigit((unsigned char)v[4])) {
		int n = atoi(v + 4);
		if (n < 0 || n > 15) return 0;
		c.r = HK_COL_ANSI; c.g = n;
	} else if (strncmp(v, "default", 7) == 0) {
		c.r = HK_COL_DEFAULT;
	} else if (sscanf(v, "%d,%d,%d", &c.r, &c.g, &c.b) == 3) {
		if (c.r < 0 || c.r > 255 || c.g < 0 || c.g > 255 || c.b < 0 || c.b > 255) return 0;
	} else {
		return 0;
	}
	*out = c;
	return 1;
}

/* A custom theme: theme.<name>.<slot>=<color>, theme.<name>.base=<preset>. */
typedef struct {
	char name[32];
	char base[32];
	struct { char slot[16]; Color c; } set[24];
	int nset;
} hkCustomTheme;
static hkCustomTheme *hk_custom;
static int hk_custom_n;

static hkCustomTheme *hkCustomFind(const char *name, int create) {
	for (int i = 0; i < hk_custom_n; i++) if (strcmp(hk_custom[i].name, name) == 0) return &hk_custom[i];
	if (!create || strlen(name) >= sizeof(hk_custom[0].name)) return NULL;
	hkCustomTheme *n = realloc(hk_custom, sizeof(*hk_custom) * (hk_custom_n + 1));
	if (!n) return NULL;
	hk_custom = n;
	memset(&hk_custom[hk_custom_n], 0, sizeof(*hk_custom));
	snprintf(hk_custom[hk_custom_n].name, sizeof(hk_custom[0].name), "%s", name);
	return &hk_custom[hk_custom_n++];
}

static int hkIsLight(Color c) {
	return c.r >= 0 && (c.r * 299 + c.g * 587 + c.b * 114) / 1000 > 128;
}

static int hkThemeLookup(const char *name, Theme *out, int depth) {
	if (!name || depth > 4) return 0;
	if (strcmp(name, "terminal") == 0) { *out = HK_THEME_TERMINAL; return 1; }
	for (int i = 0; i < (int)(sizeof(HK_THEMES) / sizeof(HK_THEMES[0])); i++) {
		if (strcmp(name, HK_THEMES[i].name) != 0) continue;
		static const size_t order[12] = {
			offsetof(Theme, bg), offsetof(Theme, fg), offsetof(Theme, comment), offsetof(Theme, keyword1),
			offsetof(Theme, keyword2), offsetof(Theme, string), offsetof(Theme, line_number),
			offsetof(Theme, status_bg), offsetof(Theme, status_fg), offsetof(Theme, border),
			offsetof(Theme, visual_bg), offsetof(Theme, visual_fg),
		};
		for (int k = 0; k < 12; k++)
			*(Color *)((char *)out + order[k]) =
				(Color){HK_THEMES[i].c[k][0], HK_THEMES[i].c[k][1], HK_THEMES[i].c[k][2], 0};
		out->number = out->string;
		out->err = hkIsLight(out->bg) ? (Color){180, 35, 30, 0} : (Color){235, 95, 80, 0};
		return 1;
	}
	hkCustomTheme *ct = hkCustomFind(name, 0);
	if (!ct) return 0;
	if (!hkThemeLookup(ct->base[0] ? ct->base : "terminal", out, depth + 1)) *out = HK_THEME_TERMINAL;
	int has_number = 0, has_string = 0;
	for (int i = 0; i < ct->nset; i++) {
		Color *slot = hkSlot(out, ct->set[i].slot);
		if (slot) *slot = ct->set[i].c;
		if (strcmp(ct->set[i].slot, "number") == 0) has_number = 1;
		if (strcmp(ct->set[i].slot, "string") == 0) has_string = 1;
	}
	if (has_string && !has_number) out->number = out->string;   /* numbers follow strings, as in the presets */
	return 1;
}

int editorApplyThemeByName(const char *name) {
	Theme t;
	if (!hkThemeLookup(name, &t, 0)) return 0;
	E.theme = t;
	snprintf(E.theme_name, sizeof(E.theme_name), "%s", name);
	return 1;
}

/* terminal first, then the presets, then the rc's own */
static int hkThemeNames(const char **out, int max) {
	int n = 0;
	if (n < max) out[n++] = "terminal";
	for (int i = 0; i < (int)(sizeof(HK_THEMES) / sizeof(HK_THEMES[0])) && n < max; i++) out[n++] = HK_THEMES[i].name;
	for (int i = 0; i < hk_custom_n && n < max; i++) out[n++] = hk_custom[i].name;
	return n;
}

void initTheme() {
	editorApplyThemeByName("terminal");
}

/* ---- the rc: one file for every tool ----
   Global: the first of $HAKORC, ~/.hakorc, ${XDG_CONFIG_HOME:-~/.config}/hako/hakorc
   (writes go there; ~/.hakorc when none exists). Project: ./.hakorc on top. An
   `edit.` key is hake's and beats the plain one; `code.` and `studio.` are the
   other tools'. A project rc comes with whatever repo was cloned, so it may not
   set what runs a command or reaches the network. */
typedef struct { char *key; char *val; int prio; } hkRcEnt;
static hkRcEnt *hk_rc;
static int hk_rc_n;

static void hkRcPut(const char *key, const char *val, int prio) {
	for (int i = 0; i < hk_rc_n; i++) {
		if (strcmp(hk_rc[i].key, key) != 0) continue;
		if (prio < hk_rc[i].prio) return;
		free(hk_rc[i].val);
		hk_rc[i].val = strdup(val);
		hk_rc[i].prio = prio;
		return;
	}
	hkRcEnt *n = realloc(hk_rc, sizeof(*hk_rc) * (hk_rc_n + 1));
	if (!n) return;
	hk_rc = n;
	hk_rc[hk_rc_n].key = strdup(key);
	hk_rc[hk_rc_n].val = strdup(val);
	hk_rc[hk_rc_n].prio = prio;
	hk_rc_n++;
}

static void hkRcFree(void) {
	for (int i = 0; i < hk_rc_n; i++) { free(hk_rc[i].key); free(hk_rc[i].val); }
	free(hk_rc);
	hk_rc = NULL;
	hk_rc_n = 0;
}

static int hkRcProjectDenied(const char *key) {
	static const char *const deny[] = {"ai_api_key", "ai_endpoint", "ai_auto_approve", "editor",
	                                   "voice_cmd", "console_sandbox", NULL};
	for (int i = 0; deny[i]; i++) if (strcmp(key, deny[i]) == 0) return 1;
	size_t n = strlen(key);
	if (n > 4 && strcmp(key + n - 4, "_cmd") == 0 && strcmp(key, "build_cmd") != 0 &&
	    strcmp(key, "test_cmd") != 0 && strcmp(key, "run_cmd") != 0) return 1;
	return 0;
}

/* Splits one rc line. Returns 0 for a comment, a blank or no '='. */
static int hkRcSplit(char *line, char **key, char **val) {
	char *s = line;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '#' || *s == '\0' || *s == '\n' || *s == '\r') return 0;
	char *eq = strchr(s, '=');
	if (!eq) return 0;
	char *ke = eq;
	while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
	*ke = '\0';
	char *v = eq + 1;
	while (*v == ' ' || *v == '\t') v++;
	v[strcspn(v, "\r\n")] = '\0';
	char *ve = v + strlen(v);
	while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = '\0';
	*key = s; *val = v;
	return *s != '\0';
}

static void hkRcReadFile(const char *path, int project) {
	FILE *fp = fopen(path, "r");
	if (!fp) return;
	char *line = NULL;
	size_t cap = 0;
	int refused = 0;
	while (getline(&line, &cap, fp) != -1) {
		char *key, *val;
		if (!hkRcSplit(line, &key, &val)) continue;
		int prefixed = 0;
		if (strncmp(key, "edit.", 5) == 0) { key += 5; prefixed = 1; }
		else if (strncmp(key, "code.", 5) == 0 || strncmp(key, "studio.", 7) == 0) continue;
		if (project && hkRcProjectDenied(key)) {
			if (!refused++)
				snprintf(E.rc_notice, sizeof(E.rc_notice), "project .hakorc: %.60s ignored (set it in ~/.hakorc)", key);
			continue;
		}
		hkRcPut(key, val, project * 2 + prefixed);
	}
	if (refused > 1) {
		size_t l = strlen(E.rc_notice);
		snprintf(E.rc_notice + l, sizeof(E.rc_notice) - l, " +%d more", refused - 1);
	}
	free(line);
	fclose(fp);
}

static const char *hkRcGet(const char *key) {
	for (int i = 0; i < hk_rc_n; i++) if (strcmp(hk_rc[i].key, key) == 0) return hk_rc[i].val;
	return NULL;
}

static int hkKeymodeFromName(const char *v) {
	if (!v) return -1;
	if (strcmp(v, "vim") == 0 || strcmp(v, "vi") == 0) return HK_KEYS_VIM;
	if (strcmp(v, "emacs") == 0) return HK_KEYS_EMACS;
	if (strcmp(v, "nano") == 0 || strcmp(v, "pico") == 0) return HK_KEYS_NANO;
	return -1;
}

static int hkThemeOverride(const char *key, const char *val) {
	static const char *const map[][2] = {
		{"theme_bg", "bg"}, {"theme_fg", "fg"}, {"theme_comment", "comment"},
		{"theme_keyword1", "keyword1"}, {"theme_keyword2", "keyword2"}, {"theme_string", "string"},
		{"theme_number", "number"}, {"theme_line_number", "line_number"},
		{"theme_status_bg", "status_bg"}, {"theme_status_fg", "status_fg"}, {"theme_border", "border"},
		{"theme_visual_bg", "visual_bg"}, {"theme_visual_fg", "visual_fg"},
	};
	for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
		if (strcmp(key, map[i][0]) != 0) continue;
		Color c;
		Color *slot = hkSlot(&E.theme, map[i][1]);
		if (slot && hkParseColor(val, &c)) *slot = c;
		return 1;
	}
	return 0;
}

/* One key, live. Shared by the rc, :set and :config. Returns 0 for a key hake
   does not know (every tool tolerates every key). */
static int hkApplyKey(const char *key, const char *val) {
	int n = atoi(val);
	if (strcmp(key, "tab_stop") == 0) { E.tab_stop = n >= 1 && n <= 16 ? n : TAB_STOP; }
	else if (strcmp(key, "use_tabs") == 0) E.use_tabs = n;
	else if (strcmp(key, "pane_titles") == 0) E.pane_titles = n;
	else if (strcmp(key, "word_wrap") == 0) {
		E.word_wrap = n;
		if (E.active_pane && E.active_pane->type == PANE_EDITOR) E.active_pane->wrap_lines = n;
	}
	else if (strcmp(key, "show_line_numbers") == 0 || strcmp(key, "number") == 0) E.show_line_numbers = n;
	else if (strcmp(key, "relative_line_numbers") == 0 || strcmp(key, "relative_numbers") == 0) { if (n) E.show_line_numbers = 2; }
	else if (strcmp(key, "status_line") == 0) E.status_line = n < 0 ? 0 : n > 2 ? 2 : n;
	else if (strcmp(key, "max_undo_levels") == 0) E.max_undo_levels = n > 0 ? n : 100;
	else if (strcmp(key, "mouse") == 0 || strcmp(key, "mouse_enabled") == 0) E.mouse_enabled = n;
	else if (strcmp(key, "auto_indent") == 0) E.auto_indent = n;
	else if (strcmp(key, "smart_indent") == 0) E.smart_indent = n;
	else if (strcmp(key, "scroll_speed") == 0) E.scroll_speed = n >= 1 ? n : 3;
	else if (strcmp(key, "explorer_width") == 0) E.explorer_width = n < 10 ? 25 : n > 60 ? 60 : n;
	else if (strcmp(key, "explorer_show_hidden") == 0) E.explorer_show_hidden = n;
	else if (strcmp(key, "console_sandbox") == 0) E.console_sandbox = n;
	else if (strcmp(key, "build_cmd") == 0) { free(E.build_cmd); E.build_cmd = strdup(val); }
	else if (strcmp(key, "test_cmd") == 0) { free(E.test_cmd); E.test_cmd = strdup(val); }
	else if (strcmp(key, "run_cmd") == 0) { free(E.run_cmd); E.run_cmd = strdup(val); }
	else if (strcmp(key, "keys") == 0) {
		int m = hkKeymodeFromName(val);
		if (m >= 0 && !E.keymode_forced) E.keymode = m;
	}
	else if (strcmp(key, "theme") == 0) {
		if (!editorApplyThemeByName(val)) {
			snprintf(E.rc_notice, sizeof(E.rc_notice), "unknown theme: %.40s (using terminal)", val);
			editorApplyThemeByName("terminal");
		}
	}
	else if (!hkThemeOverride(key, val)) return 0;
	return 1;
}

static void hkRcDefineThemes(void) {
	for (int i = 0; i < hk_rc_n; i++) {
		const char *k = hk_rc[i].key;
		if (strncmp(k, "theme.", 6) != 0) continue;
		const char *dot = strrchr(k + 6, '.');
		if (!dot || dot == k + 6) continue;
		char name[32], slot[16];
		int nl = (int)(dot - (k + 6));
		if (nl >= (int)sizeof(name) || strlen(dot + 1) >= sizeof(slot)) continue;
		memcpy(name, k + 6, nl);
		name[nl] = '\0';
		snprintf(slot, sizeof(slot), "%s", dot + 1);
		hkCustomTheme *ct = hkCustomFind(name, 1);
		if (!ct) continue;
		if (strcmp(slot, "base") == 0) { snprintf(ct->base, sizeof(ct->base), "%s", hk_rc[i].val); continue; }
		Color c;
		Theme probe;
		if (!hkSlot(&probe, slot) || !hkParseColor(hk_rc[i].val, &c)) continue;
		if (ct->nset < (int)(sizeof(ct->set) / sizeof(ct->set[0]))) {
			snprintf(ct->set[ct->nset].slot, sizeof(ct->set[0].slot), "%s", slot);
			ct->set[ct->nset].c = c;
			ct->nset++;
		}
	}
}

static int hkFileExists(const char *p) {
	struct stat st;
	return p && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* The global rc: the file that exists, or where a write would create it. */
static char *hkGlobalRcPath(int *found) {
	char buf[PATH_MAX];
	const char *env = getenv("HAKORC");
	const char *home = getenv("HOME");
	*found = 0;
	if (env && *env && hkFileExists(env)) { *found = 1; return strdup(env); }
	if (home) {
		snprintf(buf, sizeof(buf), "%s/.hakorc", home);
		if (hkFileExists(buf)) { *found = 1; return strdup(buf); }
		const char *xdg = getenv("XDG_CONFIG_HOME");
		char xb[PATH_MAX];
		if (xdg && *xdg) snprintf(xb, sizeof(xb), "%s/hako/hakorc", xdg);
		else snprintf(xb, sizeof(xb), "%s/.config/hako/hakorc", home);
		if (hkFileExists(xb)) { *found = 1; return strdup(xb); }
	}
	if (env && *env) return strdup(env);
	if (home) { snprintf(buf, sizeof(buf), "%s/.hakorc", home); return strdup(buf); }
	return NULL;
}

static int hkSameFile(const char *a, const char *b) {
#ifndef _WIN32
	struct stat sa, sb;
	return a && b && stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#else
	char ra[PATH_MAX], rb[PATH_MAX];
	return a && b && _fullpath(ra, a, PATH_MAX) && _fullpath(rb, b, PATH_MAX) && _stricmp(ra, rb) == 0;
#endif
}

void editorLoadConfig() {
	int found = 0;
	free(E.config_path);
	E.config_path = hkGlobalRcPath(&found);
	hkRcFree();
	if (found) {
		hkRcReadFile(E.config_path, 0);
#ifndef _WIN32
		/* children (the agent, a shell) read the same file */
		if (!getenv("HAKORC")) setenv("HAKORC", E.config_path, 0);
#endif
	}
	if (hkFileExists("./.hakorc") && !(found && hkSameFile("./.hakorc", E.config_path))) {
		free(E.project_rc);
		E.project_rc = strdup("./.hakorc");
		hkRcReadFile("./.hakorc", 1);
	}

	hkRcDefineThemes();
	const char *th = hkRcGet("theme");
	hkApplyKey("theme", th ? th : "terminal");
	for (int i = 0; i < hk_rc_n; i++) {
		const char *k = hk_rc[i].key;
		if (strcmp(k, "theme") == 0 || strncmp(k, "theme.", 6) == 0) continue;
		hkApplyKey(k, hk_rc[i].val);
	}
}

/* Set one key in the global rc, in place: the line that holds it is rewritten,
   every other line and comment is kept; a new key is appended. */
static int hkRcSet(const char *key, const char *val) {
	const char *path = E.config_path;
	if (!path) return 0;
	char *old = NULL;
	size_t olen = 0;
	FILE *fp = fopen(path, "r");
	if (fp) {
		char chunk[4096];
		size_t got;
		while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
			char *n = realloc(old, olen + got + 1);
			if (!n) { free(old); fclose(fp); return 0; }
			old = n;
			memcpy(old + olen, chunk, got);
			olen += got;
		}
		fclose(fp);
	}
	size_t cap = olen + strlen(key) + strlen(val) + 8;
	char *out = malloc(cap);
	if (!out) { free(old); return 0; }
	size_t o = 0;
	int replaced = 0;
	size_t i = 0;
	while (i < olen) {
		size_t e = i;
		while (e < olen && old[e] != '\n') e++;
		char *line = malloc(e - i + 1);
		if (!line) break;
		memcpy(line, old + i, e - i);
		line[e - i] = '\0';
		char *k, *v;
		int match = hkRcSplit(line, &k, &v) && strcmp(k, key) == 0;
		free(line);
		if (match) {
			size_t need = o + strlen(key) + strlen(val) + 2 + (olen - e) + 8;
			if (need > cap) {
				char *n = realloc(out, need);
				if (!n) break;
				out = n; cap = need;
			}
			o += (size_t)snprintf(out + o, cap - o, "%s=%s", key, val);
			replaced = 1;
		} else {
			memcpy(out + o, old + i, e - i);
			o += e - i;
		}
		if (e < olen) out[o++] = '\n';
		i = e + 1;
	}
	if (!replaced) {
		if (o > 0 && out[o - 1] != '\n') out[o++] = '\n';
		o += (size_t)snprintf(out + o, cap - o, "%s=%s\n", key, val);
	}
	free(old);
	int ok = 0;
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd >= 0) {
		size_t off = 0;
		while (off < o) {
			ssize_t w = write(fd, out + off, o - off);
			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) break;
			off += (size_t)w;
		}
		ok = (off == o && ftruncate(fd, (off_t)o) == 0);
		close(fd);
	}
	free(out);
	return ok;
}

static int hkRcHasKey(const char *key) {
	FILE *fp = E.config_path ? fopen(E.config_path, "r") : NULL;
	if (!fp) return 0;
	char *line = NULL;
	size_t cap = 0;
	int has = 0;
	while (!has && getline(&line, &cap, fp) != -1) {
		char *k, *v;
		if (hkRcSplit(line, &k, &v) && strcmp(k, key) == 0) has = 1;
	}
	free(line);
	fclose(fp);
	return has;
}

/* A picker writes whichever key decides hake's theme: edit.theme when the rc has
   one, else the shared theme. */
static void hkSaveTheme(const char *name) {
	const char *key = hkRcHasKey("edit.theme") ? "edit.theme" : "theme";
	if (hkRcSet(key, name)) editorSetStatusMessage("theme: %s (saved to %s)", name, hkBaseName(E.config_path));
	else editorSetStatusMessage("theme: %s (could not write %s)", name, E.config_path ? E.config_path : "~/.hakorc");
}

void initEditor() {
	E.mode = MODE_NORMAL;
	E.tab_stop = TAB_STOP;
	E.use_tabs = 1;
	E.word_wrap = 1;
	E.max_undo_levels = 100;
	E.show_line_numbers = 0;          /* vim's default; show_line_numbers=1 in the rc */
	E.status_line = 1;                /* a status bar only with more than one pane */
	E.pane_titles = 1;
	E.full_redraw_pending = 1;
	E.explorer_width = 25;
	E.mouse_enabled = 1;
	E.auto_indent = 1;
	E.smart_indent = 1;
	E.scroll_speed = 3;
	E.search.wrap_search = 1;

	E.project_dir = getcwd(NULL, 0);
	E.explorer_name = strdup("Kami");
	E.ai_name = strdup("Rei");

	initTheme();
	editorInitAI();
	editorLoadConfig();
	if (E.keymode != HK_KEYS_VIM) E.mode = MODE_INSERT;     /* emacs and nano type straight in */

	editorUpdateWindowSize();

	editorPane *pane = editorCreatePane(PANE_EDITOR, 0, 0, E.screencols, E.screenrows);
	if (pane) {
		E.root_pane = pane;
		E.active_pane = pane;
		E.num_panes = 1;
		pane->is_focused = 1;
	}
}

/* Rows below the panes: the status bar (when shown), the message line, and
   nano's two rows of shortcuts. A change of layout is not a change of terminal:
   the grid diff repaints it, no clear. */
void editorUpdateWindowSize() {
	static int last_rows = -1, last_cols = -1;
	int rows, cols;
	if (getWindowSize(&rows, &cols) == -1) die("getWindowSize");
	if (rows < 2) rows = 24;
	if (cols < 1) cols = 80;
	if (rows > 4096) rows = 4096;
	if (cols > 4096) cols = 4096;
	if (rows != last_rows || cols != last_cols) {
		if (last_rows >= 0) E.full_redraw_pending = 1;
		last_rows = rows; last_cols = cols;
	}
	int nano = E.keymode == HK_KEYS_NANO;
	E.status_shown = E.status_line == 2 || nano || (E.status_line == 1 && hkPaneCount() > 1);
	int reserved = 1 + E.status_shown + (nano ? 2 : 0);
	if (rows - reserved < 3) { E.status_shown = 0; reserved = 1; }
	int new_rows = rows - reserved;
	int new_cols = cols;
	if (new_rows != E.screenrows || new_cols != E.screencols) {
		E.screenrows = new_rows;
		E.screencols = new_cols;
		if (E.root_pane) editorResizePanes();
	}
	if (E.screencols > 0 && rows > 0) {
		gridResize(E.screencols, rows);
	}
}

void editorCleanup() {
	if (E.root_pane) {
		editorFreePane(E.root_pane);
	}
	if (E.left_panel) {
		editorFreePane(E.left_panel);
	}
	if (E.right_panel) {
		editorFreePane(E.right_panel);
	}
	if (E.bottom_panel) {
		editorFreePane(E.bottom_panel);
	}

	editorFreePasteBuffer();
	hkFreeRegisters();
	free(E.search.query);
	free(E.config_path);
	free(E.project_rc);
	free(E.build_cmd);
	free(E.test_cmd);
	free(E.run_cmd);
	hkRcFree();
	free(hk_custom);

	free(E.explorer_name);
	free(E.ai_name);

	editorCleanupAI();
	gridFree();
}

/* hake --hl-dump file: each line, then one class letter per byte of it (tabs
   expanded). tools/hl-test.py pins the highlighting with it. */
static int hkHlDump(const char *path) {
	static const char cls[] = {
		[HL_NORMAL] = '.', [HL_COMMENT] = 'c', [HL_MLCOMMENT] = 'c', [HL_KEYWORD1] = 'k', [HL_KEYWORD2] = 't',
		[HL_STRING] = 's', [HL_NUMBER] = 'n', [HL_MATCH] = '/', [HL_PREPROCESSOR] = 'p', [HL_FUNCTION] = 'f',
		[HL_TYPE] = 't', [HL_OPERATOR] = 'o', [HL_BRACKET] = 'b', [HL_CONSTANT] = 'n', [HL_BUILTIN] = 'f',
		[HL_ATTRIBUTE] = 'p', [HL_CHAR] = 's', [HL_ESCAPE] = 'e', [HL_LABEL] = 'L', [HL_HEADING] = 'h',
		[HL_STRONG] = 'B', [HL_EMPH] = 'I', [HL_STRONGEMPH] = 'X', [HL_STRIKE] = '~', [HL_CODE] = 'x',
		[HL_LINK] = 'l', [HL_URL] = 'u', [HL_MARKER] = 'm', [HL_QUOTE] = 'q', [HL_DONE] = 'd', [HL_DELIM] = '-',
		[HL_DIFF_ADD] = '+', [HL_DIFF_DEL] = '!', [HL_VARIABLE] = 'v', [HL_TAG] = 'g', [HL_ATTRNAME] = 'a',
		[HL_KEY] = 'y',
	};
	FILE *fp = fopen(path, "r");
	if (!fp) { perror(path); return 1; }
	E.tab_stop = TAB_STOP;
	char **lines = NULL;
	int *lens = NULL, n = 0, cap = 0;
	char *line = NULL;
	size_t lcap = 0;
	ssize_t got;
	while ((got = getline(&line, &lcap, fp)) != -1) {
		while (got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r')) got--;
		if (n == cap) {
			cap = cap ? cap * 2 : 64;
			lines = realloc(lines, cap * sizeof(*lines));
			lens = realloc(lens, cap * sizeof(*lens));
		}
		char *r = malloc(got * 8 + 1);
		int o = 0;
		for (ssize_t k = 0; k < got; k++) {
			if (line[k] == '\t') { do r[o++] = ' '; while (o % E.tab_stop); }
			else r[o++] = line[k];
		}
		r[o] = '\0';
		lines[n] = r;
		lens[n++] = o;
	}
	free(line);
	fclose(fp);
	const struct editorSyntax *syn = hkSyntaxForFile(path);
	if (syn == HL_TEXT && n > 0 && hkSyntaxForShebang(lines[0])) syn = hkSyntaxForShebang(lines[0]);
	printf("filetype %s\n", syn->filetype);
	int st = 0;
	for (int i = 0; i < n; i++) {
		unsigned char *hl = malloc(lens[i] + 1);
		st = hkHlLine(syn, lines[i], lens[i], hl, st, i > 0 ? lines[i - 1] : NULL, i > 0 ? lens[i - 1] : 0,
		              i + 1 < n ? lines[i + 1] : NULL, i + 1 < n ? lens[i + 1] : 0);
		printf("%s\n", lines[i]);
		for (int k = 0; k < lens[i]; k++) putchar(hl[k] < sizeof(cls) && cls[hl[k]] ? cls[hl[k]] : '?');
		putchar('\n');
		free(hl);
	}
	for (int i = 0; i < n; i++) free(lines[i]);
	free(lines);
	free(lens);
	return 0;
}
/*** main ***/
int main(int argc, char *argv[]) {
	const char *file = NULL, *dump = NULL;
	int force_keys = -1;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			printf("%s", HAKE_HELP_TEXT);
			return 0;
		}
		if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
			printf("hake v%s (build %s %s)\n", HAKE_VERSION, __DATE__, __TIME__);
			return 0;
		}
		if (strcmp(a, "--vim") == 0) force_keys = HK_KEYS_VIM;
		else if (strcmp(a, "--emacs") == 0) force_keys = HK_KEYS_EMACS;
		else if (strcmp(a, "--nano") == 0) force_keys = HK_KEYS_NANO;
		else if (strcmp(a, "--hl-dump") == 0 && i + 1 < argc) dump = argv[++i];
		else if (strcmp(a, "--") == 0) { if (i + 1 < argc) file = argv[i + 1]; break; }
		else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "hake: unknown option %s (try --help)\n", a);
			return 2;
		}
		else if (!file) file = a;
	}
	if (dump) return hkHlDump(dump);
	if (force_keys >= 0) { E.keymode = force_keys; E.keymode_forced = 1; }

	initEditor();
	detectTerminalType();
	enableRawMode();

#ifndef _WIN32
	struct sigaction sa;
	sa.sa_handler = on_sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGWINCH, &sa, NULL) == -1) {
		die("sigaction");
	}
	signal(SIGPIPE, SIG_IGN);
#endif

	if (file) editorOpen((char *)file);
	else E.intro_active = 1;
	if (E.rc_notice[0]) editorSetStatusMessage("%s", E.rc_notice);

	while (1) {
		editorReloadIfChanged();
		editorRefreshScreen();
		editorProcessKeyPress();
	}
	return 0;
}
