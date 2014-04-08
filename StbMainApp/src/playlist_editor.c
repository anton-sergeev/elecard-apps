#include "playlist_editor.h"

#include "dvbChannel.h"

#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"

static int channelNumber = -1;

void getDVBList(){

}

void saveList(){

}

void addList(){

}
int lockChannel()
{
    return 1;
}
int unlockChannel()
{
    return 0;
}
static int color_save = -1;

void setColor(int color) {
    color_save = color;
}

int getColor() {
    return color_save;
}

void set_lockColor(){
    setColor(interfaceInfo.highlightColor);

    interfaceInfo.highlightColor++;
    if (interface_colors[interfaceInfo.highlightColor].A==0)
        interfaceInfo.highlightColor = 0;
}
void set_unLockColor(){
    interfaceInfo.highlightColor = getColor();
    setColor(-1);
    if (interface_colors[interfaceInfo.highlightColor].A==0)
        interfaceInfo.highlightColor = 0;
}

int getChannelEditor(){
    printf("channelNumber = %d\n",channelNumber);
    return channelNumber;
}
int get_statusLockPlaylist()
{
    if (channelNumber == -1)
        return false;
    return true;
}

static int swapMenu;

int playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
    if (channelNumber == -1) {
        swapMenu = CHANNEL_INFO_GET_CHANNEL(pArg);
        channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

        set_lockColor();
        //return output_saveAndRedraw(saveAppSettings(), pMenu);
    } else {

        channelNumber = -1;
        set_unLockColor();
        printf("\n\nsritch %d -> %d\n\n\n",swapMenu, CHANNEL_INFO_GET_CHANNEL(pArg));
     //   interface_switchMenuEntryCustom(pMenu, swapMenu,  CHANNEL_INFO_GET_CHANNEL(pArg));
       // interface_switchMenu(pMenu,swapMenu);
        swapMenu = -1;
    }
    interface_displayMenu(1);
    return 0;
}

void createPlaylist(interfaceMenu_t  *interfaceMenu)
{
    if (!(interfaceMenu && !interfaceMenu->menuEntryCount))
        return;
    interfaceMenu_t *channelMenu = interfaceMenu;
    struct list_head *pos;
    char channelEntry[MENU_ENTRY_INFO_LENGTH];
    int32_t i = 0;
    interface_clearMenuEntries(channelMenu);

    //offair_updateChannelStatus();
    //	interface_addMenuEntryDisabled(channelMenu, "DVB", 0);

    list_for_each(pos, dvbChannel_getSortList()) {
        service_index_t *srv = list_entry(pos, service_index_t, orderSort);
        EIT_service_t *service = srv->service;
        interfaceMenuEntry_t *entry;
        char *serviceName;
        int32_t radio;

        if(!srv->visible) {
            continue;
        }
        radio = service->service_descriptor.service_type == 2;
        serviceName = dvb_getServiceName(service);

        snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), serviceName);
        interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), dvb_getScrambled(service) ? thumbnail_billed : (radio ? thumbnail_radio : thumbnail_channels));

        entry = menu_getLastEntry(channelMenu);
        if(entry) {
            interface_setMenuEntryLabel(entry, "VISIBLE");
        }
        if(appControlInfo.dvbInfo.channel == i) {
            interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
        }
        i++;
    }
}


int enterPlaylistEditorMenu(interfaceMenu_t *interfaceMenu, void* pArg)
{

    printf("%s[%d]\n",__func__, __LINE__);
    createPlaylist(interfaceMenu);
/*
    offair_createPlaylist(&pMenu);
    printf("%s[%d]\n",__func__, __LINE__);
    if((dvbChannel_getCount() == 0) && (analogtv_getChannelCount() == 0)) {
        output_showDVBMenu(pMenu, NULL);
        interface_showConfirmationBox( _T("DVB_NO_CHANNELS"), thumbnail_dvb, offair_confirmAutoScan, NULL);
        return 1;
    }

    printf("%s[%d]\n",__func__, __LINE__);
    offair_channelChange(interfaceInfo.currentMenu, CHANNEL_INFO_SET(screenMain, appControlInfo.dvbInfo.channel));
    return 0;
*/
    return 0;
    /*
    char buf[MENU_ENTRY_INFO_LENGTH];
    interface_clearMenuEntries(interfaceMenu);
    printf("%s[%d]\n");
    snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYCONTROL_SHOW_ON_START"), interfacePlayControl.showOnStart ? _T("ON") : _T("OFF") );
    interface_addMenuEntry(interfaceMenu, buf, output_togglePlayControlShowOnStart, NULL, settings_interface);
    return 0;
*/
}

