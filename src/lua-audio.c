#include "audio.h"
#include "lua-audio.h"

#include <lua.h>
#include <lauxlib.h>
#include <skalibs/skalibs.h>

#ifdef __cplusplus
extern "C" {
#endif

static int
lua_amp_index(lua_State *L) {
    int index = 0;
    audio_processor *a = NULL;
    if(!lua_isnumber(L,2)) {
        return 0;
    }
    index = lua_tointeger(L,2) - 1;
    if(index < 0) {
        return 0;
    }
    lua_getfield(L,1,"data");
    a = lua_touserdata(L,-1);
    lua_pop(L,1);

    if((unsigned int)index >= a->spectrum_len) {
        return 0;
    }

    lua_pushnumber(L,a->spectrum_cur[index].amp);
    return 1;
}


int luaopen_audio(lua_State *L,audio_processor *a) {
    unsigned int i = 0;
    luaL_newmetatable(L,"amp");
    lua_pushcfunction(L,lua_amp_index);
    lua_setfield(L,-2,"__index");
    lua_pop(L,1);

    lua_newtable(L); /* audio */
    lua_pushinteger(L,a->samplerate);
    lua_setfield(L,-2,"samplerate");

    lua_pushinteger(L,a->channels);
    lua_setfield(L,-2,"channels");

    lua_pushinteger(L,a->samplesize);
    lua_setfield(L,-2,"samplesize");

    lua_pushinteger(L,a->spectrum_len);
    lua_setfield(L,-2,"spectrum_len");

    lua_newtable(L); /* audio.freqs */
    for(i=0;i<a->spectrum_len;i++) {
        lua_pushinteger(L,i+1);
        lua_pushnumber(L,a->spectrum_cur[i].freq);
        lua_rawset(L,-3);
    }
    lua_setfield(L,-2,"freqs");

    lua_newtable(L); /* audio.amps */
    lua_pushlightuserdata(L,a);
    lua_setfield(L,-2,"data");
    luaL_getmetatable(L,"amp");
    lua_setmetatable(L,-2);
    lua_setfield(L,-2,"amps");

    return 1;
}

#ifdef __cplusplus
}
#endif


