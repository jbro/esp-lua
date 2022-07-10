#include "lua_repl.h"

void app_main(void)
{
  init_console();

  while (1)
  {
    lua_State *L = luaL_newstate(); /* create state */
    if (L == NULL)
    {
      printf("cannot create state: not enough memory");
      goto error;
    }
    luaL_openlibs(L);           /* open standard libraries */
    lua_gc(L, LUA_GCGEN, 0, 0); /* GC in generational mode */
    doREPL(L);                  /* do read-eval-print loop */
  error:
    lua_close(L);
  }
}
