#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

void doREPL(lua_State *L);
void print_version(void);

void lua_repl(void)
{
  lua_State *L = luaL_newstate();
  if (L == NULL)
  {
    printf("cannot create state: not enough memory");
    goto error;
  }
  luaL_openlibs(L);
  lua_gc(L, LUA_GCGEN, 0, 0);
  print_version();
  doREPL(L);
error:
  lua_close(L);
}