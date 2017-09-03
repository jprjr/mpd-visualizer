#include "tinydir.h"
#include "lua-file.h"

#include <lua.h>
#include <lauxlib.h>
#include <skalibs/skalibs.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(luaL_newlibtable) \
  && (!defined LUA_VERSION_NUM || LUA_VERSION_NUM==501)
static void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup+1, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    lua_pushlstring(L, l->name,strlen(l->name));
    for (i = 0; i < nup; i++)  /* copy upvalues to the top */
      lua_pushvalue(L, -(nup+1));
    lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    lua_settable(L, -(nup + 3));
  }
  lua_pop(L, nup);  /* remove upvalues */
}
#endif

static int
lua_file_list(lua_State *L) {
    const char *folder = luaL_checkstring(L,1);

    tinydir_dir dir;
    unsigned long int i;
    int j = 1;
    if(tinydir_open_sorted(&dir,folder) == -1) {
        return 0;
    }
    lua_newtable(L);

    for(i=0;i<dir.n_files;i++) {
        tinydir_file file;
        struct stat st;
        tinydir_readfile_n(&dir,&file,i);
        if(file.is_dir) {
            continue;
        }
        stat(file.path,&st);
        lua_pushinteger(L,j);
        lua_newtable(L);
        lua_pushlstring(L,file.path,strlen(file.path));
        lua_setfield(L,-2,"file");
        lua_pushinteger(L,st.st_mtime);
        lua_setfield(L,-2,"mtime");
        lua_settable(L,-3);
        j++;
    }

    tinydir_close(&dir);

    return 1;
}


static const struct luaL_Reg lua_file_methods[] = {
    { "ls"       , lua_file_list },
    { NULL     , NULL                },
};

int luaopen_file(lua_State *L) {
    lua_newtable(L);
    luaL_setfuncs(L,lua_file_methods,0);
    lua_setglobal(L,"file");

    return 0;
}

#ifdef __cplusplus
}
#endif
