#ifndef PLAYLIST_EDITOR_H
#define PLAYLIST_EDITOR_H
#include "interface.h"

int enterPlaylistEditorMenu(interfaceMenu_t *pMenu, void* pArg);
void playList_saveName(int ,char* , char*);
void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int count);
// int push_playlist();
void playlist_editor_setupdate();
int getChannelEditor();
void playlist_switchElement(int , int );
void playlist_editor_cleanup();
int get_statusLockPlaylist();
void set_unLockColor();
int check_playlist();

#endif // PLAYLIST_EDITOR_H
