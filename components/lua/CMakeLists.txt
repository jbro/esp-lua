idf_component_register(SRCS "lua/onelua.c" "lua_repl.c"
                    INCLUDE_DIRS lua .
                    REQUIRES console)

execute_process(
  WORKING_DIRECTORY ${COMPONENT_DIR}/lua
  COMMAND git reset --hard)
execute_process(
  WORKING_DIRECTORY ${COMPONENT_DIR}/lua
  COMMAND git checkout v${CONFIG_ESP_LUA_VERSION})
execute_process(
  WORKING_DIRECTORY ${COMPONENT_DIR}/lua
  COMMAND git apply ../lua-${CONFIG_ESP_LUA_VERSION}.patch)
