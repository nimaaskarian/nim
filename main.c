// vim:foldmethod=marker
// Includes {{{
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
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
  int size;
  char *buffer;
} erow;
struct editorConfig {
  struct appendBuffer sequence;
  struct appendBuffer numberSequence;
  long int numberSequenceInt;
  enum Mode mode;
  int cursorx, cursory;
  int rowoffset, coloffset;
  int screenrows, screencols;
  unsigned int numrows;
  erow* row;
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

  // we want the last row to be for our commands
  *rows-=1;
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
    // we want the last row to be for our commands
    *rows = ws.ws_row-1;
    return EXIT_SUCCESS;
  }
}
// }}}
// Row operations {{{
void editorAppendRow(char *s, size_t len)
{
  EDITOR.row = realloc(EDITOR.row, sizeof(erow) * (EDITOR.numrows+1));

  int at = EDITOR.numrows;
  EDITOR.row[at].size = len;
  EDITOR.row[at].buffer = malloc(len+1);
  memcpy(EDITOR.row[at].buffer, s, len);
  EDITOR.row[at].buffer[len] = '\0';
  EDITOR.numrows++;
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

  while ((linelen = getline(&line, &linecap, fptr)) != -1)
    if (linelen != -1) {
      while (linelen > 0 && (line[linelen - 1] == '\n' ||
                             line[linelen - 1] == '\r'))
        linelen--;

      editorAppendRow(line, linelen);
    }

  free(line);
  fclose(fptr);
  
}
// }}}
// Output {{{
void editorScroll()
{
  if (EDITOR.cursory < EDITOR.rowoffset)
    EDITOR.rowoffset = EDITOR.cursory;
  if (EDITOR.cursory >= EDITOR.rowoffset + EDITOR.screenrows)
    EDITOR.rowoffset = EDITOR.cursory - EDITOR.screenrows + 1;

  if (EDITOR.cursorx < EDITOR.coloffset)
    EDITOR.coloffset = EDITOR.cursorx;
  if (EDITOR.cursorx >= EDITOR.coloffset + EDITOR.screencols)
    EDITOR.coloffset = EDITOR.cursorx - EDITOR.screencols + 1;
}

void editorDrawRows(struct appendBuffer *ab)
{
  for (int y = 0; y < EDITOR.screenrows; y++) {
    int filerow = y+EDITOR.rowoffset;
    if (filerow >= EDITOR.numrows) {
      if (EDITOR.numrows == 0 && y == EDITOR.screenrows/3) {
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
      int len = EDITOR.row[filerow].size - EDITOR.coloffset;
      if (len < 0) 
        len = 0;
      abAppend(ab, &EDITOR.row[filerow].buffer[EDITOR.coloffset], len);
    }

    // erase in line
    abAppend(ab, "\x1b[K", 3);

    if(y < EDITOR.screenrows - 1)
      abAppend(ab, "\r\n", 2);
  }
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

  abAppend(&ab, "\x1b[H", 3);

  // move cursor to cursor position
  char buf[32];
  if (EDITOR.mode == MODE_COMMAND)
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", EDITOR.screencols, 1);
  else
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (EDITOR.cursory-EDITOR.rowoffset)+1,
                                              (EDITOR.cursorx-EDITOR.coloffset)+1);
  abAppend(&ab, buf, strlen(buf));

  // show cursor (unset mode ?25 which is hidden)
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  abFree(&ab);
}
// }}}
// Input {{{
void editorMoveCursorToLine(long int *linenumber)
{

  if (*linenumber > EDITOR.numrows)
    *linenumber=EDITOR.numrows;

  if (*linenumber < 0)
    *linenumber = -*linenumber;

  EDITOR.cursory = *linenumber;
  EDITOR.rowoffset = 0;
  *linenumber=0;
}

struct erow* getCurrentRow()
{
  return (EDITOR.cursory >= EDITOR.numrows) ? NULL : &EDITOR.row[EDITOR.cursory];
}
void editorMoveCursor (int key) 
{
  erow *currentRow = getCurrentRow();

  switch (key) {
    case KEY_LEFT:
      if (EDITOR.cursorx != 0)
        EDITOR.cursorx--;
      break;
    case KEY_RIGHT:
      if (currentRow && EDITOR.cursorx < currentRow->size)
        EDITOR.cursorx++;
      break;
    CASE_DOWN:
      if (EDITOR.cursory < EDITOR.numrows)
        EDITOR.cursory++;
      break;
    CASE_UP:
      if (EDITOR.cursory > 0)
        EDITOR.cursory--;
      break;
    case BOTTOM:
      EDITOR.cursory = EDITOR.numrows - 1;
      EDITOR.rowoffset = 0;
      break;
    case TOP:
      EDITOR.cursory = 0;
      EDITOR.rowoffset = 0;
      break;
    case KEY_LINE_FIRST:
      EDITOR.cursorx = 0;
      break;
    case KEY_LINE_START:
      EDITOR.cursorx = firstNonSpace(currentRow->buffer);
      break;
    case KEY_LINE_END:
      EDITOR.cursorx = currentRow->size;
    break;
  }
  currentRow = getCurrentRow();

  int currentRowLength = currentRow ? currentRow->size : 0;
  if (EDITOR.cursorx > currentRowLength)
    EDITOR.cursorx = currentRowLength;
}

void editorHandleNormalMode(char keyChar) {
  if (isdigit(keyChar)) {
    if (keyChar != '0' || EDITOR.numberSequence.length > 0) {
      abAppend(&EDITOR.numberSequence, &keyChar, 1);
      return;
    }
  }

  if (EDITOR.numberSequence.length) {

    char *output;
    EDITOR.numberSequenceInt= strtol(EDITOR.numberSequence.buffer, &output, 10);
    abReinit(&EDITOR.numberSequence);
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
      do 
        editorMoveCursor(keyChar);
      while (--EDITOR.numberSequenceInt > 0);
      break;
    case BOTTOM:
      if (EDITOR.numberSequenceInt) 
        editorMoveCursorToLine(&EDITOR.numberSequenceInt);
      else
        editorMoveCursor(keyChar);
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
  EDITOR.numrows = 0;
  EDITOR.rowoffset = 0;
  EDITOR.coloffset = 0;
  EDITOR.numberSequenceInt = 0;
  EDITOR.mode = MODE_NORMAL;
  EDITOR.row = NULL;

  abReinit(&EDITOR.sequence);
  abReinit(&EDITOR.numberSequence);

  if (getWindowSize(&EDITOR.screenrows, &EDITOR.screencols) == EXIT_FAILURE)
    die("getWindowSize");
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
  free(EDITOR.row);
  return EXIT_SUCCESS;
}
// }}}
