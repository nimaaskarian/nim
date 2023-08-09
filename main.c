// vim:foldmethod=marker
// Includes {{{
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
// }}}
// Defines {{{
#define CTRL_KEY(k) ((k) & 0x1f)
#define NIM_VERSION "0.0.1"
enum EditorKey {
  LEFT = 'h',
  RIGHT = 'l',
  UP = 'k',
  DOWN = 'j',
  TOP = 1000,
};
enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_REPLACE,
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
// }}}
// }}}
// Data {{{
struct editorConfig {
  struct appendBuffer sequence;
  int cursorx, cursory;
  int screenrows, screencols;
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
// Output {{{
void editorDrawRows(struct appendBuffer *ab)
{
  for (int y = 0; y < EDITOR.screenrows; y++) {
    if (y == EDITOR.screenrows/3) {
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

    // erase in line
    abAppend(ab, "\x1b[K", 3);

    if(y < EDITOR.screenrows - 1)
      abAppend(ab, "\r\n", 2);
  }
}
void editorRefreshScreen() 
{
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
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", EDITOR.cursory+1, EDITOR.cursorx+1);
  abAppend(&ab, buf, strlen(buf));

  // show cursor (unset mode ?25 which is hidden)
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  abFree(&ab);
}
// }}}
// Input {{{
void editorMoveCursor (int key) 
{
  switch (key) {
    case LEFT:
      if (EDITOR.cursorx != 0)
        EDITOR.cursorx--;
      break;
    case RIGHT:
      if (EDITOR.cursorx != EDITOR.screencols - 1)
        EDITOR.cursorx++;
      break;
    case DOWN:
      EDITOR.cursory++;
      break;
    case UP:
      EDITOR.cursory--;
      break;

    case TOP:
      EDITOR.cursory = 0;
      break;
  }
}
void editorProcessKeypress(char keyChar) {
  if (EDITOR.sequence.length) {
    if (strcmp(EDITOR.sequence.buffer, "gg")) {
      editorMoveCursor(TOP);
      abReinit(&EDITOR.sequence);
    }
  }
  switch (keyChar) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(EXIT_SUCCESS);
      break;
    case 'g':
      abAppend(&EDITOR.sequence, "g" ,1);
      break;
    case RIGHT:
    case LEFT:
    case DOWN:
    case UP:
      editorMoveCursor(keyChar);
      break;
  }
}
// }}}
// Init {{{
void initEditor()
{
  EDITOR.cursorx = 0;
  EDITOR.cursory = 0;
  abReinit(&EDITOR.sequence);

  if (getWindowSize(&EDITOR.screenrows, &EDITOR.screencols) == EXIT_FAILURE)
    die("getWindowSize");
}
int main()
{ 
  enableRawMode();
  initEditor();
  
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress(editorReadKey());
  }
  abFree(&EDITOR.sequence);
  return EXIT_SUCCESS;
}
// }}}
