// vim:foldmethod=marker
// Includes {{{
#include <stdarg.h>
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

size_t firstNonSpace (const char* s, int start)
{
  size_t output = start;
  while(isspace(s[output]))
  {
    ++output;
  }
  return output;
}
size_t reversedFirstNonSpace(const char* s, int start)
{
  size_t output = start;
  while(isspace(s[output]))
  {
    --output;
    if (output < 0)
      return -1;
  }
  return output;
}
// }}}
// Defines {{{
#define CTRL_KEY(k) ((k) & 0x1f)
#define NIM_VERSION "0.0.1"
#define TAB_WIDTH 1
enum EditorKey {
  KEY_LEFT = 'h',
  KEY_RIGHT = 'l',
  KEY_UP = 'k',
  KEY_DOWN = 'j',
  KEY_LINE_START = '^',
  KEY_LINE_FIRST = '0',
  KEY_LINE_END = '$',
  WORD_NEXT = 'w',
  WORD_END = 'e',
  WORD_BACK = 'b',
  BOTTOM = 'G',
  ESC = 27,
  BACKSPACE = 127,
  ENTER = 13,
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
typedef struct EditorRow {
  int size, rendersize;
  char *buffer, *renderbuffer;
} EditorRow;
struct Editor {
  struct appendBuffer sequence;
  struct appendBuffer numberSequence;
  int numberSequenceInt;
  enum Mode mode;
  int cursorx, cursory;
  int savedcursorx;
  int renderx;
  int isEndMode;
  int rowoffset, coloffset;
  int screenrows, screencols;
  unsigned int rowscount;
  EditorRow* rows;
  EditorRow commandRow;
  char *filename;
  struct termios orig_termios;
};
struct Editor editor;
EditorRow* getCurrentRow()
{
  if (editor.cursory >= editor.rowscount)
    return NULL;
  
  return &editor.rows[editor.cursory];
}
// }}}
// Range {{{
struct Range {
  int start, end;
};
struct Range createRange(int min, int max)
{
  struct Range r;
  r.start = min;
  r.end = max;
  return r;
}
int isInRange(int search, struct Range range)
{
  if (search < range.start || search > range.end)
    return 0;

  return 1;
}
// }}}
// Terminal {{{
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &editor.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = editor.orig_termios;
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
int editorRowCursorxToRenderx(EditorRow *row, int cursorx)
{
  int renderx = 0;
  for (int i = 0; i < cursorx; ++i) {
    if(row->buffer[i] == '\t')
      renderx += (TAB_WIDTH - 1) - (renderx % TAB_WIDTH);
    renderx++;
  }

  return renderx;
}
int editorRowRenderxToCursorx(EditorRow *row, int renderx)
{
  int cursorx = 0;
  for (int i = editor.cursorx; i < renderx; ++i) {
    if(row->buffer[i] == '\t')
      cursorx -= (TAB_WIDTH - 1) + (cursorx % TAB_WIDTH);
    cursorx++;
  }
  return cursorx;
}
void editorUpdateRow(EditorRow *row) 
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
  editor.rows = realloc(editor.rows, sizeof(EditorRow) * (editor.rowscount+1));

  int at = editor.rowscount;
  editor.rows[at].size = len;
  editor.rows[at].buffer = malloc(len+1);
  memcpy(editor.rows[at].buffer, s, len);

  editor.rows[at].buffer[len] = '\0';

  editor.rows[at].rendersize = 0;
  editor.rows[at].renderbuffer = NULL;
  editorUpdateRow(&editor.rows[at]);

  editor.rowscount++;
}
void editorRowInsertChar(EditorRow *row, int index, int charToInsert) {
  if (index < 0 || index > row->size) index = row->size;
  row->buffer = realloc(row->buffer, row->size + 2);
  memmove(&row->buffer[index + 1], &row->buffer[index], row->size - index + 1);
  row->size++;
  row->buffer[index] = charToInsert;
  editorUpdateRow(row);
}
void editorRowDelChar(EditorRow *row, int index) {
  if (index < 0 || index >= row->size) return;
  memmove(&row->buffer[index], &row->buffer[index + 1], row->size - index);
  row->size--;
  editorUpdateRow(row);
  // editor.dirty++;
}
// }}}
// Editor operations {{{
void editorQuit()
{
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  exit(EXIT_SUCCESS);
}
void editorExecuteCommandRow()
{
  int start = 0;

  while (editor.commandRow.buffer[start] == ':') {
    start++;
  }
  editor.commandRow.size-=start;
  memmove(editor.commandRow.buffer, editor.commandRow.buffer+start, editor.commandRow.size);
  editor.commandRow.buffer[editor.commandRow.size] = '\0';

  if (strcmp(editor.commandRow.buffer,"q") == 0) 
  {
    editorQuit();
  }
}
void editorCommandChar (int keyChar)
{
  if (keyChar == ENTER) {
    editorExecuteCommandRow();
    free(editor.commandRow.buffer);
    editor.commandRow.buffer = NULL;
    editor.commandRow.size = 0;
    editor.mode = MODE_NORMAL;
    return;
  }
  if (keyChar == BACKSPACE) {
    editorRowDelChar(&editor.commandRow, editor.commandRow.size-1);
    return;
  }
  if (keyChar == CTRL_KEY('u')) {
    free(editor.commandRow.buffer);
    editor.commandRow.buffer = NULL;
    editor.commandRow.size = 0;
    editorRowInsertChar(&editor.commandRow,editor.commandRow.size, ':');
    return;
  }

  if (iscntrl(keyChar))
    return;

  editorRowInsertChar(&editor.commandRow,editor.commandRow.size, keyChar);
}
void editorInsertChar(int keyChar) 
{
  if (keyChar == BACKSPACE) {
    editorRowDelChar(getCurrentRow(), editor.cursorx-1);
    editor.cursorx--;
    return;
  }

  if (editor.cursory == editor.rowscount) {
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&editor.rows[editor.cursory], editor.cursorx, keyChar);
  editor.cursorx++;
}
// }}}
// File i/o {{{
void editorOpen(char *filename) 
{
  free(editor.filename);
  editor.filename = strdup(filename);

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
enum WordType {
  WT_NON_WORD = 0,
  WT_WORD,
  WT_SPACE,
};
enum WordType editorCharWordType(unsigned char ch)
{
  if (isspace(ch))
    return WT_SPACE;
  if (isInRange(ch, createRange('a', 'z')))
    return WT_WORD;
  if (isInRange(ch, createRange('A', 'Z')))
    return WT_WORD;
  if (isInRange(ch, createRange('0', '9')))
    return WT_WORD;
  // code below is from vim iskeyword. first one is inclusive, second is exclusive
  // so we minus one the second one
  if (isInRange(ch,  createRange(192, 254)))
    return WT_WORD;
  if (ch == '_')
    return WT_WORD;

  return WT_NON_WORD;
}
void editorScroll()
{
  editor.renderx = 0;
  if (editor.cursory < editor.rowscount)
    editor.renderx = editorRowCursorxToRenderx(&editor.rows[editor.cursory], editor.cursorx);

  if (editor.cursory < editor.rowoffset)
    editor.rowoffset = editor.cursory;
  if (editor.cursory >= editor.rowoffset + editor.screenrows)
    editor.rowoffset = editor.cursory - editor.screenrows + 1;

  if (editor.cursorx < editor.coloffset)
    editor.coloffset = editor.cursorx;
  if (editor.cursorx >= editor.coloffset + editor.screencols)
    editor.coloffset = editor.cursorx - editor.screencols + 1;

  if (editor.renderx < editor.coloffset)
    editor.coloffset = editor.renderx;
  if (editor.renderx >= editor.coloffset + editor.screencols)
    editor.coloffset = editor.renderx - editor.screencols + 1;

  if (editor.rows[editor.cursory].size <= editor.screencols)
    editor.coloffset = 0;
}

void editorDrawRows(struct appendBuffer *ab)
{
  for (int y = 0; y < editor.screenrows; y++) {
    int filerow = y+editor.rowoffset;
    if (filerow >= editor.rowscount) {
      if (editor.rowscount == 0 && y == editor.screenrows/3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Nim editor -- version %s", NIM_VERSION);
        if (welcomelen > editor.screencols)
          welcomelen = editor.screencols;

        int padding = (editor.screencols - welcomelen) / 2;
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
      int len = editor.rows[filerow].rendersize - editor.coloffset;
      if (len < 0) 
        len = 0;
      if (len > editor.screencols)
        len = editor.screencols;
      abAppend(ab, &editor.rows[filerow].renderbuffer[editor.coloffset], len);
    }

    // erase in line
    abAppend(ab, "\x1b[K", 3);

    // if(y < EDITOR.screenrows - 1)
      abAppend(ab, "\r\n", 2);
  }
}
int editorDrawCommand(struct appendBuffer *ab) 
{

  abAppend(ab, editor.commandRow.buffer, editor.commandRow.size);
  return editor.commandRow.size;
}
void editorDrawStatusBar(struct appendBuffer *ab) 
{
  // invert colors
  // abAppend(ab, "\x1b[7m", 4);
  int len = 0;
  if (editor.mode == MODE_COMMAND) {
    len += editorDrawCommand(ab);
  }

  while (len < editor.screencols) {
    if (len == editor.screencols - 17) {
      char buf[13];
      int bufLength = snprintf(buf, sizeof(buf), "%d,%d", editor.cursory+1, editor.cursorx+1);
      abAppend(ab, buf, bufLength);
      len+=bufLength;
    } else if (len == editor.screencols-4) {
      char buf[4];
      int scrollPercentage = round((double)editor.rowoffset/(editor.rowscount-editor.screenrows)*100);
      if (editor.rowscount < editor.screenrows)
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
  if (editor.mode == MODE_COMMAND)
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editor.screencols, editor.commandRow.size+1);
  else
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (editor.cursory-editor.rowoffset)+1,
                                              (editor.renderx-editor.coloffset)+1);
  abAppend(&ab, buf, strlen(buf));

  // show cursor (unset mode ?25 which is hidden)
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  abFree(&ab);
}
// }}}
// Input {{{
int editorEndOfTheWord(int start) {
  int end = start;

  // Find current word boundary
  while (isalnum(getCurrentRow()->buffer[end]) && end <= getCurrentRow()->size) {
      end++;
  }

  // Move cursor to end of current word
  return end;
}
void editorSetCursorx(int x)
{
  editor.cursorx = x;
  editor.savedcursorx = x;
}
void applySavedcursorx()
{
  if (editor.savedcursorx) {
    if (getCurrentRow()->size - 1 > editor.savedcursorx) {
      // editor.cursorx = editorRowRenderxToCursorx(currentRow, editor.savedcursorx);
      editor.cursorx = editor.savedcursorx;
    } else {
      editor.cursorx = getCurrentRow()->size-1;
    }
  }

}
void editorHandleMoveCursorNormal (int key);
void editorMoveCursoryToLine(int *linenumber)
{
  if (*linenumber > editor.rowscount)
    *linenumber=editor.rowscount;

  if (*linenumber < 0)
    *linenumber = -(*linenumber);

  editor.cursory = *linenumber-1;
  editor.rowoffset = 0;
  *linenumber=0;
  // move cursor with zero key so it gets aligned
  editorHandleMoveCursorNormal(0);
}

int editorMoveCursorDown()
{
  if (editor.cursory < editor.rowscount - 1) {
    editor.cursory++;
    applySavedcursorx();
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

int editorMoveCursorUp()
{
  if (editor.cursory > 0) {
    editor.cursory--;
    applySavedcursorx();
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

void editorMoveCursorRight()
{
  EditorRow *currentRow = getCurrentRow();

  editor.isEndMode = 0;
  if (currentRow && editor.cursorx < currentRow->size - 1)
    editorSetCursorx(editor.cursorx+1);
  else if(currentRow && editor.cursorx == currentRow->size - 1) {
    editor.cursory++;
    editorSetCursorx(0);
  }
}

void editorMoveCursorLeft()
{
  editor.isEndMode = 0;
  if (editor.cursorx != 0) {
    editorSetCursorx(editor.cursorx-1);
  } else {
    editorHandleMoveCursorNormal(KEY_UP);
    if (editor.cursory)
      editorSetCursorx(getCurrentRow()->size);
  }
}

int isRowAllSpace(EditorRow *er)
{
  for (int i = 0; i < er->size; i++)
    if (!isspace(er->buffer[i]))
      return 0;

  return 1;
}

int currentSequenceLastIndex(int start) {
  EditorRow *currentRow = getCurrentRow();
  for (int i = start; i < currentRow->size; ++i) 
    if (editorCharWordType(currentRow->buffer[i]) != editorCharWordType(currentRow->buffer[i+1]))
      return i;

  return currentRow->size - 1;
}

int currentSequenceFirstIndex(int start) {
  EditorRow *currentRow = getCurrentRow();
  for (int i = start-1; i > -1; --i) 
    if (editorCharWordType(currentRow->buffer[i]) != editorCharWordType(currentRow->buffer[i+1]))
      return i;

  return firstNonSpace(currentRow->buffer,0);
}

void editorMoveCursorWordEnd()
{
  EditorRow *currentRow = getCurrentRow();

  if (editor.cursorx >= currentRow->size - 1)
    if (editorMoveCursorDown() == EXIT_SUCCESS) {
      currentRow = getCurrentRow();
      editor.cursorx = 0;
    }
  while(isRowAllSpace(currentRow)){
    if (editorMoveCursorDown() == EXIT_FAILURE)
      break;
    editor.cursorx = 0;
    currentRow = getCurrentRow();
  }

  char currentChar = currentRow->buffer[editor.cursorx];
  int lastIndex = currentSequenceLastIndex(editor.cursorx);

  if (editor.cursorx == lastIndex) {
    editor.cursorx=firstNonSpace(currentRow->buffer, editor.cursorx+1);
    editor.cursorx=currentSequenceLastIndex(editor.cursorx);
  } else
    editor.cursorx = lastIndex;

}

void editorMoveCursorWordBack()
{
  EditorRow *currentRow = getCurrentRow();

  if (editor.cursorx <= 0)
    if (editorMoveCursorUp() == EXIT_SUCCESS) {
      currentRow = getCurrentRow();
      editorSetCursorx(currentRow->size-1);
    }

  char currentChar = currentRow->buffer[editor.cursorx];
  int lastIndex = currentSequenceFirstIndex(editor.cursorx);

  if (editor.cursorx == lastIndex) {
    editorSetCursorx(reversedFirstNonSpace(currentRow->buffer, editor.cursorx+1));
    editorSetCursorx(currentSequenceFirstIndex(editor.cursorx));
  } else
     editorSetCursorx(lastIndex);

  while (isspace(currentRow->buffer[editor.cursorx])) {
    editorSetCursorx(editor.cursorx-1);
    if (editor.cursorx <= 0) {
      break;
    }
  }

  while(isRowAllSpace(getCurrentRow())){
    if (editorMoveCursorUp() == EXIT_FAILURE)
      break;
    editor.cursorx = getCurrentRow()->size-1;
  }

}

void editorMoveCursorWordStart()
{
  EditorRow *currentRow = getCurrentRow();

  char currentChar = currentRow->buffer[editor.cursorx];
  if (isspace(currentChar)) {
    editor.cursorx = firstNonSpace(currentRow->buffer, editor.cursorx);
    return;
  }

  for (int i = editor.cursorx; i < currentRow->size; ++i) {
    if (editorCharWordType(currentRow->buffer[i]) != editorCharWordType(currentChar)) {
      while (isspace(currentRow->buffer[i])) {
        i++;
        if (i == currentRow->size-1) {
          editorMoveCursorDown();
          currentRow = getCurrentRow();
        }
      }
      editor.cursorx = i;
      return;
    }
  }

  if (editorMoveCursorDown() == EXIT_SUCCESS) {
    editor.cursorx = firstNonSpace(getCurrentRow()->buffer, 0);
  }

  while(isRowAllSpace(getCurrentRow())){
    if (editorMoveCursorDown() == EXIT_FAILURE)
      break;
    editor.cursorx = 0;
  }
}

void editorHandleMoveCursorNormal (int key)
{
  EditorRow *currentRow = getCurrentRow();

  switch (key) {
    case WORD_NEXT:
      editorMoveCursorWordStart();
      break;
    case WORD_END:
      editorMoveCursorWordEnd();
      break;
    case WORD_BACK:
      editorMoveCursorWordBack();
      break;
    case KEY_RIGHT:
      editorMoveCursorRight();
      break;
    CASE_DOWN:
      editorMoveCursorDown();
      break;
    case KEY_LEFT:
      editorMoveCursorLeft();
      break;
    CASE_UP:
      editorMoveCursorUp();
      break;
    case BOTTOM:
      editor.cursory = editor.rowscount - 1;
      break;
    case TOP:
      editor.cursory = 0;
      break;
    case KEY_LINE_FIRST:
      editor.isEndMode = 0;
      editor.cursorx = 0;
      break;
    case KEY_LINE_START:
      editor.isEndMode = 0;
      editor.cursorx = firstNonSpace(currentRow->buffer, 0);
      break;
    case KEY_LINE_END:
      editor.cursorx = currentRow? currentRow->size-1:0;
      editor.isEndMode = 1;
      break;
  }

  currentRow = getCurrentRow();

  int currentRowLength = currentRow ? currentRow->size-1 : 0;
  if (editor.cursorx > currentRowLength || editor.isEndMode)
    editor.cursorx = currentRowLength;

  // first set cursory to 0 if its below 0.
  // so rowscount comparison be valid
  if (editor.cursory < 0)
    editor.cursory = 0;

  if (editor.cursorx < 0)
    editor.cursorx = 0;

  if (editor.cursory > editor.rowscount)
    editor.cursory = editor.rowscount;


  while (editor.cursory + editor.rowoffset > editor.rowscount && editor.rowoffset > 0)
    editor.rowoffset--;


}

void editorHandleNormalMode(char keyChar) {
  if (isdigit(keyChar)) {
    if (keyChar != '0' || editor.numberSequence.length > 0) {
      abAppend(&editor.numberSequence, &keyChar, 1);
      return;
    }
  } else {
    if (editor.numberSequence.length) {
      editor.numberSequenceInt= atoi(editor.numberSequence.buffer);
      abReinit(&editor.numberSequence);
    }
  }

  switch (keyChar) {
    case 'i':
      editor.mode = MODE_INSERT;
      break;
    case ':':
      editor.mode = MODE_COMMAND;
      editorRowInsertChar(&editor.commandRow,editor.commandRow.size, ':');
    break;
    case 'g':
      abAppend(&editor.sequence, &keyChar ,1);
      break;
    case KEY_RIGHT:
    case KEY_LEFT:
    case KEY_LINE_START:
    case KEY_LINE_FIRST:
    case KEY_LINE_END:
    case WORD_NEXT:
    case WORD_END:
    case WORD_BACK:
    CASE_DOWN:
    CASE_UP:
      if (editor.numberSequenceInt <= 0)
        editorHandleMoveCursorNormal(keyChar);
      else {
        while (editor.numberSequenceInt > 0) {
          editorHandleMoveCursorNormal(keyChar);
          editor.numberSequenceInt--;
        }
      }
      break;
    case BOTTOM:
      if (editor.numberSequenceInt) 
        editorMoveCursoryToLine(&editor.numberSequenceInt);
      else
        editorHandleMoveCursorNormal(keyChar);
      break;
    case CTRL_KEY('f'):
      editor.cursory+=editor.screenrows;
      editorHandleMoveCursorNormal(0);
      break;
    case CTRL_KEY('b'):
      editor.cursory-=editor.screenrows;
      editorHandleMoveCursorNormal(0);
      break;
  }
  if (editor.sequence.length > 1) {
    if (strstr(editor.sequence.buffer, "gg") != NULL) {
      if (editor.numberSequenceInt)
        editorMoveCursoryToLine(&editor.numberSequenceInt);
      else
        editorHandleMoveCursorNormal(TOP);
      
    }
    abReinit(&editor.sequence);
  }
}
// }}}
// Init {{{
void initEditor()
{
  editor.cursorx = 0;
  editor.savedcursorx = 0;
  editor.cursory = 0;
  editor.rowscount = 0;
  editor.rowoffset = 0;
  editor.coloffset = 0;
  editor.numberSequenceInt = 0;
  editor.mode = MODE_NORMAL;
  editor.rows = NULL;
  editor.filename = NULL;
  editor.isEndMode = 0;

  editor.commandRow.buffer = NULL;
  editor.commandRow.size = 0;

  abReinit(&editor.sequence);
  abReinit(&editor.numberSequence);

  if (getWindowSize(&editor.screenrows, &editor.screencols) == EXIT_FAILURE)
    die("getWindowSize");
  // we want the last row to be for our commands
  editor.screenrows -= 1;
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
      abReinit(&editor.sequence);
      abReinit(&editor.numberSequence);

      editor.commandRow.buffer = NULL;
      editor.commandRow.size = 0;

      editor.mode = MODE_NORMAL;
    }
    else {
      switch (editor.mode) {
        case MODE_NORMAL:
          editorHandleNormalMode(ch);
          break;
        case MODE_INSERT:
          editorInsertChar(ch);
          break;
        case MODE_COMMAND:
          editorCommandChar(ch);
          if (editor.commandRow.size == 0)
            editor.mode = MODE_NORMAL;
          break;
      }
    }
  } 
  abFree(&editor.sequence);
  abFree(&editor.numberSequence);
  free(editor.rows);
  return EXIT_SUCCESS;
}
// }}}
