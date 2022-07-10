#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"

#if !defined(LUA_MAXINPUT)
#define LUA_MAXINPUT 512
#endif

#if !defined(LUA_PROMPT)
#define LUA_PROMPT "> "
#define LUA_PROMPT2 ">> "
#endif

#define lua_readline(L, b, p) ((void)L, ((b) = linenoise(p)) != NULL)
#define lua_freeline(L, b) ((void)L, linenoiseFree(b))
#define lua_saveline(L, line) ((void)L, linenoiseHistoryAdd(line))

#define EOFMARK "<eof>"
#define marklen (sizeof(EOFMARK) / sizeof(char) - 1)

static const char *progname = "lua";

static lua_State *globalL = NULL;

static int pushline(lua_State *L, int firstline);

static void l_message(const char *pname, const char *msg)
{
  if (pname)
    lua_writestringerror("%s: ", pname);
  lua_writestringerror("%s\n", msg);
}

static int report(lua_State *L, int status)
{
  if (status != LUA_OK)
  {
    const char *msg = lua_tostring(L, -1);
    l_message(progname, msg);
    lua_pop(L, 1); /* remove message */
  }
  return status;
}

static void l_print(lua_State *L)
{
  int n = lua_gettop(L);
  if (n > 0)
  { /* any result to be printed? */
    luaL_checkstack(L, LUA_MINSTACK, "too many results to print");
    lua_getglobal(L, "print");
    lua_insert(L, 1);
    if (lua_pcall(L, n, 0, 0) != LUA_OK)
      l_message(progname, lua_pushfstring(L, "error calling 'print' (%s)",
                                          lua_tostring(L, -1)));
  }
}

static int msghandler(lua_State *L)
{
  const char *msg = lua_tostring(L, 1);
  if (msg == NULL)
  {                                          /* is error object not a string? */
    if (luaL_callmeta(L, 1, "__tostring") && /* does it have a metamethod */
        lua_type(L, -1) == LUA_TSTRING)      /* that produces a string? */
      return 1;                              /* that is the message */
    else
      msg = lua_pushfstring(L, "(error object is a %s value)",
                            luaL_typename(L, 1));
  }
  luaL_traceback(L, L, msg, 1); /* append a standard traceback */
  return 1;                     /* return the traceback */
}

static int docall(lua_State *L, int narg, int nres)
{
  int status;
  int base = lua_gettop(L) - narg;  /* function index */
  lua_pushcfunction(L, msghandler); /* push message handler */
  lua_insert(L, base);              /* put it under function and args */
  globalL = L;                      /* to be available to 'laction' */
  // setsignal(SIGINT, laction);       /* set C-signal handler */
  status = lua_pcall(L, narg, nres, base);
  // setsignal(SIGINT, SIG_DFL); /* reset C-signal handler */
  lua_remove(L, base); /* remove message handler from the stack */
  return status;
}

static const char *get_prompt(lua_State *L, int firstline)
{
  if (lua_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2") == LUA_TNIL)
    return (firstline ? LUA_PROMPT : LUA_PROMPT2); /* use the default */
  else
  { /* apply 'tostring' over the value */
    const char *p = luaL_tolstring(L, -1, NULL);
    lua_remove(L, -2); /* remove original value */
    return p;
  }
}

static int pushline(lua_State *L, int firstline)
{
  char buffer[LUA_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  int readstatus = lua_readline(L, b, prmt);
  if (readstatus == 0)
    return 0;    /* no input (prompt will be popped by caller) */
  lua_pop(L, 1); /* remove prompt */
  l = strlen(b);
  if (l > 0 && b[l - 1] == '\n')            /* line ends with newline? */
    b[--l] = '\0';                          /* remove it */
  if (firstline && b[0] == '=')             /* for compatibility with 5.2, ... */
    lua_pushfstring(L, "return %s", b + 1); /* change '=' to 'return' */
  else
    lua_pushlstring(L, b, l);
  lua_freeline(L, b);

  printf("\n");
  return 1;
}

static int incomplete(lua_State *L, int status)
{
  if (status == LUA_ERRSYNTAX)
  {
    size_t lmsg;
    const char *msg = lua_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0)
    {
      lua_pop(L, 1);
      return 1;
    }
  }
  return 0; /* else... */
}

static int multiline(lua_State *L)
{
  for (;;)
  { /* repeat until gets a complete statement */
    size_t len;
    const char *line = lua_tolstring(L, 1, &len);         /* get what it has */
    int status = luaL_loadbuffer(L, line, len, "=stdin"); /* try it */
    if (!incomplete(L, status) || !pushline(L, 0))
    {
      lua_saveline(L, line); /* keep history */
      return status;         /* cannot or should not try to add continuation line */
    }
    lua_pushliteral(L, "\n"); /* add newline... */
    lua_insert(L, -2);        /* ...between the two lines */
    lua_concat(L, 3);         /* join them */
  }
}

static int addreturn(lua_State *L)
{
  const char *line = lua_tostring(L, -1); /* original line */
  const char *retline = lua_pushfstring(L, "return %s;", line);
  int status = luaL_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == LUA_OK)
  {
    lua_remove(L, -2);       /* remove modified line */
    if (line[0] != '\0')     /* non empty? */
      lua_saveline(L, line); /* keep history */
  }
  else
    lua_pop(L, 2); /* pop result from 'luaL_loadbuffer' and modified line */
  return status;
}

static int loadline(lua_State *L)
{
  int status;
  lua_settop(L, 0);
  if (!pushline(L, 1))
    return -1;                           /* no input */
  if ((status = addreturn(L)) != LUA_OK) /* 'return ...' did not work? */
    status = multiline(L);               /* try as command, maybe with continuation lines */
  lua_remove(L, 1);                      /* remove line from the stack */
  lua_assert(lua_gettop(L) == 1);
  return status;
}

static void doREPL(lua_State *L)
{
  int status;
  const char *oldprogname = progname;
  progname = NULL; /* no 'progname' on errors in interactive mode */
  while ((status = loadline(L)) != -1)
  {
    if (status == LUA_OK)
      status = docall(L, 0, LUA_MULTRET);
    if (status == LUA_OK)
      l_print(L);
    else
      report(L, status);
  }
  lua_settop(L, 0); /* clear stack */
  lua_writeline();
  progname = oldprogname;
}

void app_main(void)
{
  fflush(stdout);
  fsync(fileno(stdout));
  setvbuf(stdin, NULL, _IONBF, 0);

  esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
  esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

  const uart_config_t uart_config = {
      .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .source_clk = UART_SCLK_REF_TICK,
  };
  ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                      256, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

  esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

  esp_console_config_t console_config = {
      .max_cmdline_args = 8,
      .max_cmdline_length = 256,
      .hint_color = atoi(LOG_COLOR_CYAN)};
  ESP_ERROR_CHECK(esp_console_init(&console_config));

  const char *prompt = CONFIG_IDF_TARGET "> ";

  linenoiseSetMultiLine(1);
  linenoiseHistorySetMaxLen(10);

  while (1)
  {
    int status, result;
    lua_State *L = luaL_newstate(); /* create state */
    if (L == NULL)
    {
      printf("cannot create state: not enough memory");
      goto error;
    }
    luaL_openlibs(L);           /* open standard libraries */
    lua_gc(L, LUA_GCGEN, 0, 0); /* GC in generational mode */
    doREPL(L);                  /* do read-eval-print loop */
    lua_close(L);
  }
error:
  lua_close(L);
}
