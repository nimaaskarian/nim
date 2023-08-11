// vim:foldmethod=marker
// Includes {{{
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
// }}}
// Assets {{{
int charToDigit(char ch) 
{
  return ch - '0';
} 

size_t firstNonSpace (const char* s) 
{
  size_t i = 0;
  while(s[i] == ' ' || s[i] == '\t'|| s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')
  {
      ++i;
  }
  return i;
}
// }}}
// Defines {{{
#define CTRL_KEY(k) ((k) & 0x1f)
#define NIM_VERSION "0.0.1"
#define TAB_WIDTH 4
enum EditorKey {
  KEY_LEFT = 'h',
  KEY_RIGHT = 'l',
  KEY_UP = 'k',
  KEY_DOWN = 'j',
  KEY_LINE_START = '^',
  KEY_LINE_FIRST = '0',
  KEY_LINE_END = '$',
  BOTTOM = 'G',
  ESC = 27,
  TOP = 1000,
};
#define CASE_DOWN case KEY_DOWN: case '+'
#define CASE_UP case KEY_UP: case '-'
enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_REPLACE,
  MODE_COMMAND,
};
// Append buffer {{{
struct appendBuffer {
  char *buffer;
  int length;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct appendBuffer *ab, const char *s, int len) 
{
  char *newCharptr = realloc(ab->buffer, ab->length + len);

  if (newCharptr == NULL)
    return;

  memcpy(&newCharptr[ab->length], s, len);
  ab->buffer = newCharptr;
  ab->length += len;
}

void abFree(struct appendBuffer *ab)
{
  free(ab->buffer);
}

void abReinit(struct appendBuffer *ab)
{
  ab->buffer = NULL;
  ab->length = 0;
}
void abEmpty(struct appendBuffer *ab)
{
  ab->buffer = "";
  ab->length = 0;
}
// }}}
// }}}
// Data {{{
typedef struct erow {
  int size, rendersize;
  char *buffer, *renderbuffer;
} erow;
struct editorConfig {
  struct appendBuffer sequence;
  struct appendBuffer numberSequence;
  int numberSequenceInt;
  enum Mode mode;
  int cursorx, cursory;
  int renderx;
  int isEndMode;
  int rowoffset, coloffset;
  int screenrows, screencols;
  unsigned int rowscount;
  erow* rows;
  struct termios orig_termios;
};
struct editorConfig EDITOR;
// }}}
// Terminal {{{
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &EDITOR.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &EDITOR.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = EDITOR.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |=  (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  printf("%c\r\n", c);
  return c;
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO,"\x1b[6n",4) != 4)
    return EXIT_FAILURE;

  while (i < sizeof(buf) - 1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R') 
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') 
    return EXIT_FAILURE;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) 
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

int getWindowSize(int *rows, int *cols) 
{
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return EXIT_FAILURE;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return EXIT_SUCCESS;
  }
}
// }}}
// Row operations {{{
int editorRowCursorxToRenderx(erow *row, int cursorx)
{
  int renderx = 0;
  for (int i = 0; i < cursorx; ++i) {
    if(row->buffer[i] == '\t')
      renderx += (TAB_WIDTH - 1) - (renderx % TAB_WIDTH);
    renderx++;
  }

  return renderx;
}
void editorUpdateRow(erow *row) 
{
  int tabs = 0;
  for (int i = 0; i < row->size; i++)
    if(row->buffer[i] == '\t')
      tabs++;

  free(row->renderbuffer);
  row->renderbuffer = malloc(row->size+1 + tabs*(TAB_WIDTH-1));

  int index = 0;
  for (int i = 0; i < row->size; ++i)
  {
    if (row->buffer[i] == '\t') {
      row->renderbuffer[index++] = ' ';
      while (index % TAB_WIDTH != 0)
        row->renderbuffer[index++] = ' ';
    } else
      row->renderbuffer[index++] = row->buffer[i];
  }

  row->renderbuffer[index] = '\0';
  row->rendersize = index;
}
void editorAppendRow(char *s, size_t len)
{
  EDITOR.rows = realloc(EDITOR.rows, sizeof(erow) * (EDITOR.rowscount+1));

  int at = EDITOR.rowscount;
  EDITOR.rows[at].size = len;
  EDITOR.rows[at].buffer = malloc(len+1);
  memcpy(EDITOR.rows[at].buffer, s, len);

  EDITOR.rows[at].buffer[len] = '\0';

  EDITOR.rows[at].rendersize = 0;
  EDITOR.rows[at].renderbuffer = NULL;
  editorUpdateRow(&EDITOR.rows[at]);

  EDITOR.rowscount++;
}
// }}}
// File i/o {{{
void editorOpen(char *filename) 
{
  FILE *fptr = fopen(filename, "r");
  if (!fptr) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fptr)) != -1) {
    if (linelen != -1) {
      while (linelen > 0 && (line[linelen - 1] == '\n' ||
                             line[linelen - 1] == '\r'))
        linelen--;

      editorAppendRow(line, linelen);
    }
}

  free(line);
  fclose(fptr);
  
}
// }}}
// Output {{{
void editorScroll()
{
  EDITOR.renderx = 0;
  if (EDITOR.cursory < EDITOR.rowscount)
    EDITOR.renderx = editorRowCursorxToRenderx(&EDITOR.rows[EDITOR.cursory], EDITOR.cursorx);

  if (EDITOR.cursory < EDITOR.rowoffset)
    EDITOR.rowoffset = EDITOR.cursory;
  if (EDITOR.cursory >= EDITOR.rowoffset + EDITOR.screenrows)
    EDITOR.rowoffset = EDITOR.cursory - EDITOR.screenrows + 1;

  if (EDITOR.cursorx < EDITOR.coloffset)
    EDITOR.coloffset = EDITOR.cursorx;
  if (EDITOR.cursorx >= EDITOR.coloffset + EDITOR.screencols)
    EDITOR.coloffset = EDITOR.cursorx - EDITOR.screencols + 1;

  if (EDITOR.renderx < EDITOR.coloffset)
    EDITOR.coloffset = EDITOR.renderx;
  if (EDITOR.renderx >= EDITOR.coloffset + EDITOR.screencols)
    EDITOR.coloffset = EDITOR.renderx - EDITOR.screencols + 1;

  if (EDITOR.rows[EDITOR.cursory].size <= EDITOR.screencols)
    EDITOR.coloffset = 0;
}

void editorDrawRows(struct appendBuffer *ab)
{
  for (int y = 0; y < EDITOR.screenrows; y++) {
    int filerow = y+EDITOR.rowoffset;
    if (filerow >= EDITOR.rowscount) {
      if (EDITOR.rowscount == 0 && y == EDITOR.screenrows/3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Nim editor -- version %s", NIM_VERSION);
        if (welcomelen > EDITOR.screencols)
          welcomelen = EDITOR.screencols;

        int padding = (EDITOR.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);

        abAppend(ab,welcome, welcomelen);

      } else 
        abAppend(ab, "~", 1);
    } else {
      int len = EDITOR.rows[filerow].rendersize - EDITOR.coloffset;
      if (len < 0) 
        len = 0;
      if (len > EDITOR.screencols)
        len = EDITOR.screencols;
      abAppend(ab, &EDITOR.rows[filerow].renderbuffer[EDITOR.coloffset], len);
    }

    // erase in line
    abAppend(ab, "\x1b[K", 3);

    // if(y < EDITOR.screenrows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct appendBuffer *ab) 
{
  // invert colors
  // abAppend(ab, "\x1b[7m", 4);
  int len = 0;
  while (len < EDITOR.screencols) {
    if (len == EDITOR.screencols - 17) {
      char buf[13];
      int bufLength = snprintf(buf, sizeof(buf), "%d,%d", EDITOR.cursory+1, EDITOR.cursorx+1);
      abAppend(ab, buf, bufLength);
      len+=bufLength;
    } else if (len == EDITOR.screencols-4) {
      char buf[4];
      int scrollPercentage = round((double)EDITOR.rowoffset/(EDITOR.rowscount-EDITOR.screenrows)*100);
      if (EDITOR.rowscount < EDITOR.screenrows)
        scrollPercentage = -1;
      
      int bufLength = 0;
      switch (scrollPercentage) {
        case -1:
        bufLength = snprintf(buf, sizeof(buf), "%s", "All");
        break;
        case 0:
        bufLength = snprintf(buf, sizeof (buf), "%s", "Top");
        break;
        case 100:
        bufLength = snprintf(buf, sizeof (buf), "%s", "Bot");
        break;
        default:
        bufLength = snprintf(buf, sizeof (buf), "%d%%", scrollPercentage);
        break;
      }
      abAppend(ab, buf, bufLength);
      len+=bufLength;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  // abAppend(ab, "\x1b[m", 3);
}

void editorRefreshScreen() 
{
  editorScroll();
  struct appendBuffer ab = ABUF_INIT;

  // hide cursor (set mode ?25 which is hidden)
  abAppend(&ab, "\x1b[?25l", 6);

  // clear entire screen (removed because of erase in line)
  // abAppend(&ab, "\x1b[2J", 4);

  // move to top left corner (1;1H cause default is 1)
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);

  abAppend(&ab, "\x1b[H", 3);

  // move cursor to cursor position
  char buf[32];
  if (EDITOR.mode == MODE_COMMAND)
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", EDITOR.screencols, 1);
  else
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EDITOR.cursory-EDITOR.rowoffset)+1,
                                              (EDITOR.renderx-EDITOR.coloffset)+1);
  abAppend(&ab, buf, strlen(buf));

  // show cursor (unset mode ?25 which is hidden)
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  abFree(&ab);
}
// }}}
// Input {{{
void editorMoveCursor (int key);
void editorMoveCursorToLine(int *linenumber)
{
  if (*linenumber > EDITOR.rowscount)
    *linenumber=EDITOR.rowscount;

  if (*linenumber < 0)
    *linenumber = -(*linenumber);

  EDITOR.cursory = *linenumber-1;
  EDITOR.rowoffset = 0;
  *linenumber=0;
  // move cursor with zero key so it gets aligned
  editorMoveCursor(0);
}

struct erow* getCurrentRow()
{
  if (EDITOR.cursory >= EDITOR.rowscount)
    return NULL;
  
  return &EDITOR.rows[EDITOR.cursory];
}
void editorMoveCursor (int key)
{
  erow *currentRow = getCurrentRow();

  switch (key) {
    case KEY_RIGHT:
      EDITOR.isEndMode = 0;
      if (currentRow && EDITOR.cursorx < currentRow->size)
        EDITOR.cursorx++;
      else if(currentRow && EDITOR.cursorx == currentRow->size) {
        EDITOR.cursory++;
        EDITOR.cursorx=0;
      }
      break;
    CASE_DOWN:
      if (EDITOR.cursory < EDITOR.rowscount - 1)
        EDITOR.cursory++;
      break;
    case KEY_LEFT:
      EDITOR.isEndMode = 0;
      if (EDITOR.cursorx != 0) {
        EDITOR.cursorx--;
      } else if (EDITOR.cursory > 0) {
        EDITOR.cursory--;
        EDITOR.cursorx = getCurrentRow()->size;
      }
      break;
    CASE_UP:
      if (EDITOR.cursory > 0)
        EDITOR.cursory--;
      break;
    case BOTTOM:
      EDITOR.cursory = EDITOR.rowscount - 1;
      break;
    case TOP:
      EDITOR.cursory = 0;
      break;
    case KEY_LINE_FIRST:
      EDITOR.isEndMode = 0;
      EDITOR.cursorx = 0;
      break;
    case KEY_LINE_START:
      EDITOR.isEndMode = 0;
      EDITOR.cursorx = firstNonSpace(currentRow->renderbuffer);
      break;
    case KEY_LINE_END:
      EDITOR.cursorx = currentRow? currentRow->size:0;
      EDITOR.isEndMode = 1;
      break;
  }
  currentRow = getCurrentRow();

  int currentRowLength = currentRow ? currentRow->size : 0;
  if (EDITOR.cursorx > currentRowLength || EDITOR.isEndMode)
    EDITOR.cursorx = currentRowLength;

  // first set cursory to 0 if its below 0.
  // so rowscount comparison be valid
  if (EDITOR.cursory < 0)
    EDITOR.cursory = 0;

  if (EDITOR.cursory > EDITOR.rowscount)
    EDITOR.cursory = EDITOR.rowscount;

  while (EDITOR.cursory + EDITOR.rowoffset > EDITOR.rowscount && EDITOR.rowoffset > 0)
    EDITOR.rowoffset--;
}

void editorHandleNormalMode(char keyChar) {
  if (isdigit(keyChar)) {
    if (keyChar != '0' || EDITOR.numberSequence.length > 0) {
      abAppend(&EDITOR.numberSequence, &keyChar, 1);
      return;
    }
  } else {
    if (EDITOR.numberSequence.length) {
      EDITOR.numberSequenceInt= atoi(EDITOR.numberSequence.buffer);
      abReinit(&EDITOR.numberSequence);
    }
  }

  switch (keyChar) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(EXIT_SUCCESS);
      break;
    case ':':
      EDITOR.mode = MODE_COMMAND;
      // disableRawMode();
    break;
    case 'g':
      abAppend(&EDITOR.sequence, &keyChar ,1);
      break;
    case KEY_RIGHT:
    case KEY_LEFT:
    case KEY_LINE_START:
    case KEY_LINE_FIRST:
    case KEY_LINE_END:
    CASE_DOWN:
    CASE_UP:
      if (EDITOR.numberSequenceInt <= 0)
        editorMoveCursor(keyChar);
      else {
        while (EDITOR.numberSequenceInt > 0) {
          editorMoveCursor(keyChar);
          EDITOR.numberSequenceInt--;
        }
      }
      break;
    case BOTTOM:
      if (EDITOR.numberSequenceInt) 
        editorMoveCursorToLine(&EDITOR.numberSequenceInt);
      else
        editorMoveCursor(keyChar);
      break;
    case CTRL_KEY('f'):
      EDITOR.cursory+=EDITOR.screenrows;
      editorMoveCursor(0);
      break;
    case CTRL_KEY('b'):
      EDITOR.cursory-=EDITOR.screenrows;
      editorMoveCursor(0);
      break;
  }
  if (EDITOR.sequence.length > 1) {
    if (strstr(EDITOR.sequence.buffer, "gg") != NULL) {
      if (EDITOR.numberSequenceInt)
        editorMoveCursorToLine(&EDITOR.numberSequenceInt);
      else
        editorMoveCursor(TOP);
      
    }
    abReinit(&EDITOR.sequence);
  }
}
// }}}
// Init {{{
void initEditor()
{
  EDITOR.cursorx = 0;
  EDITOR.cursory = 0;
  EDITOR.rowscount = 0;
  EDITOR.rowoffset = 0;
  EDITOR.coloffset = 0;
  EDITOR.numberSequenceInt = 0;
  EDITOR.mode = MODE_NORMAL;
  EDITOR.rows = NULL;
  EDITOR.isEndMode = 0;

  abReinit(&EDITOR.sequence);
  abReinit(&EDITOR.numberSequence);

  if (getWindowSize(&EDITOR.screenrows, &EDITOR.screencols) == EXIT_FAILURE)
    die("getWindowSize");
  // we want the last row to be for our commands
  EDITOR.screenrows -= 1;
}
int main(int argc, char *argv[])
{ 
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);
  
  while (1) {
    editorRefreshScreen();
    char ch = editorReadKey();
    if (ch == ESC) {
      abReinit(&EDITOR.sequence);
      EDITOR.mode = MODE_NORMAL;
    }
    else {
      if (EDITOR.mode == MODE_NORMAL)
        editorHandleNormalMode(ch);
    }
  } 
  abFree(&EDITOR.sequence);
  abFree(&EDITOR.numberSequence);
  free(EDITOR.rows);
  return EXIT_SUCCESS;
}
// }}}
