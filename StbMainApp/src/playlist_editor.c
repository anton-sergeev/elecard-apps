#include "playlist_editor.h"

#include "dvbChannel.h"
#include "output.h"
#include "bouquet.h"
#include "analogtv.h"
#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"
#include "l10n.h"
#include "md5.h"

typedef struct _playListEditorDigital_t
{
	int radio;
	int scrambled;
	service_index_t *service_index;
	service_index_data_t data;
} playListEditorDigital_t;

typedef struct _playListEditorAnalog_t
{
	uint32_t frequency;
	//analog_service_t *service_index;
	service_index_data_t data;
} playListEditorAnalog_t;

static int channelNumber = -1;
list_element_t *playListEditorDigital = NULL;
list_element_t *playListEditorAnalog = NULL;
static int update_list = false;
static int color_save = -1;
static int swapMenu;


void setColor(int color) {
	color_save = color;
}

int getColor() {
	return color_save;
}

static void load_Analog_channels(list_element_t *curListEditor)
{
	extern analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];
	int analogtv_channelCount = analogtv_getChannelCount(1);
	int i;
	for(i = 0; i < analogtv_channelCount; i++) {
		list_element_t      *cur_element;
		playListEditorAnalog_t    *element;

		if (playListEditorAnalog == NULL) {
			cur_element = playListEditorAnalog = allocate_element(sizeof(playListEditorAnalog_t));
		} else {
			cur_element = append_new_element(playListEditorAnalog, sizeof(playListEditorAnalog_t));
		}
		if (!cur_element)
			break;
		element = (playListEditorAnalog_t *)cur_element->data;
		element->frequency =  analogtv_channelParam[i].frequency;
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", analogtv_channelParam[i].customCaption);
		element->data.visible = analogtv_channelParam[i].visible;
		element->data.parent_control = analogtv_channelParam[i].parent_control;
	}
}

static void load_digital_channels(list_element_t *curListEditor)
{
	extern dvb_channels_t g_dvb_channels;
	struct list_head *pos;

	list_for_each(pos, &g_dvb_channels.orderNoneHead) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

		if(srvIdx == NULL)
			continue;

		list_element_t      *cur_element;
		playListEditorDigital_t    *element;

		if (playListEditorDigital == NULL) {
			cur_element = playListEditorDigital = allocate_element(sizeof(playListEditorDigital_t));
		} else {
			cur_element = append_new_element(playListEditorDigital, sizeof(playListEditorDigital_t));
		}
		if (!cur_element)
			break;

		element = (playListEditorDigital_t *)cur_element->data;
		element->service_index = srvIdx;
		element->data.visible = srvIdx->data.visible;
		element->data.parent_control = srvIdx->data.parent_control;

		if(	(srvIdx->service->service_descriptor.service_type == 2) ||
			(dvb_hasMediaType(srvIdx->service, mediaTypeAudio) && !dvb_hasMediaType(srvIdx->service, mediaTypeVideo)))
		{
			element->radio = 1;
		}
		element->scrambled = dvb_getScrambled(srvIdx->service);
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
	}
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

void playlist_editor_cleanup(typeBouquet_t index){
/*	if (index == 0 || index == 1)
		free_elements(&playListEditorDigital);
	if (index == 0 || index == 2)
		free_elements(&playListEditorAnalog);*/
	if (index == fullBouquet || index == digitalBouquet)
		free_elements(&playListEditorDigital);
	if (index == fullBouquet || index == analogBouquet)
		free_elements(&playListEditorAnalog);
}

int getChannelEditor(){
	return channelNumber;
}
int get_statusLockPlaylist()
{
	if (channelNumber == -1)
		return false;
	return true;
}

void playList_saveName(int num, char *prev_name, char *new_name)
{
	int i = 0;
	list_element_t *cur_element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		playListEditorDigital_t *element;
		for(cur_element = playListEditorDigital; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorDigital_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (num == i && strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))){
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
				return;
			}
			i++;
		}
		return;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		playListEditorAnalog_t *element;
		for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorAnalog_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (num == i && strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))){
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
				return;
			}
			i++;
		}
		return;
	}
}

void playlist_editor_setupdate(){
	update_list = true;
}

void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, int count)
{
	int i = 0;
	list_element_t *cur_element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		playListEditorDigital_t *element;
		for(cur_element = playListEditorDigital; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorDigital_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (count == i){
				char desc[20];
				if(element->data.visible) {
					if(element->data.parent_control) {
						element->data.parent_control = false;
						element->data.visible = false;
						snprintf(desc, sizeof(desc), "INVISIBLE");
					} else {
						element->data.parent_control = true;
						snprintf(desc, sizeof(desc), "PARENT");
					}
				} else {
					element->data.visible = true;
					snprintf(desc, sizeof(desc), "VISIBLE");
				}
				interface_changeMenuEntryLabel(pMenuEntry, desc, strlen(desc) + 1);
				interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);
				interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
				return;
			}
			i++;
		}
		return;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		playListEditorAnalog_t *element;
		for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorAnalog_t*)cur_element->data;
			if(element == NULL)
				continue;
			if (count == i){
				char desc[20];
				if(element->data.visible) {
					element->data.visible = false;
					snprintf(desc, sizeof(desc), "INVISIBLE");
				} else {
					element->data.visible = true;
					snprintf(desc, sizeof(desc), "VISIBLE");
				}
				interface_changeMenuEntryLabel(pMenuEntry, desc, strlen(desc) + 1);
				interface_changeMenuEntryThumbnail(pMenuEntry, element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);
				interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
				return;
			}
			i++;
		}
		return;
	}
}

int push_playlist()
{
	if ((playListEditorDigital == NULL &&  playListEditorAnalog == NULL) || update_list == false)
		return false;
	update_list = false;
	int first = 0;
	int i = 0;
	list_element_t		*service_element;

	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		service_element = playListEditorDigital;
		while (service_element != NULL) {
			playListEditorDigital_t *curElement = (playListEditorDigital_t *)service_element->data;
			if (curElement->service_index != NULL) {
				curElement->service_index->data.visible        = curElement->data.visible;
				curElement->service_index->data.parent_control = curElement->data.parent_control;
				snprintf(curElement->service_index->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", curElement->data.channelsName);
			}
			first = dvbChannel_findNumberService(curElement->service_index);
			if ( i > first)
				dvbChannel_swapServices(first, i);
			else
				dvbChannel_swapServices(i, first);

			i++;
			service_element = service_element->next;
		}
		return true;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		service_element = playListEditorAnalog;
		extern analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];
		int index;
		while (service_element != NULL) {
			playListEditorAnalog_t *curElement = (playListEditorAnalog_t *)service_element->data;
			service_element = service_element->next;
			index = analogtv_findOnFrequency(curElement->frequency);
			if (index == -1)
				continue;
			analogtv_channelParam[index].visible = curElement->data.visible;
			analogtv_channelParam[index].parent_control = curElement->data.parent_control;
			snprintf(analogtv_channelParam[index].customCaption, MENU_ENTRY_INFO_LENGTH, "%s", curElement->data.channelsName);

			analogtv_swapService(index, i);
			i++;
		}
		analogtv_saveConfigFile();
		bouquet_saveAnalogBouquet(NULL, NULL);
		return true;
	}
	return -1;
}

int playList_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	unsigned char out[16];
	char out_hex[32];
	char pass[32];
	if(value == NULL) {
		return 0;
	}

	md5((unsigned char *)value, strlen(value), out);
	for (int i=0;i<16;i++) {
		sprintf(&out_hex[i*2], "%02hhx", out[i]);
	}
	FILE *pass_file = fopen(PARENT_CONTROL_FILE, "r");
	if(pass_file == NULL) {
		char cmd[256];
		pass_file = fopen(PARENT_CONTROL_DEFAULT_FILE, "r");
		if(pass_file == NULL) {
			return 0;
		}
		snprintf(cmd, sizeof(cmd), "cp %s %s", PARENT_CONTROL_DEFAULT_FILE, PARENT_CONTROL_FILE);
		system(cmd);
	}
	fread(pass, 32, 1, pass_file);
	fclose(pass_file);

	if(strncmp(out_hex, pass, 32) == 0) {
		push_playlist();
		dvbChannel_applyUpdates();
	}
	else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		return 1;
	}

	return 0;
}

int check_playlist()
{
	if ((playListEditorDigital == NULL && playListEditorAnalog == NULL) || update_list == false)
		return false;
	extern interfaceListMenu_t InterfacePlaylistEditorDigital;

	list_element_t		*service_element = playListEditorDigital;
	while (service_element != NULL) {
		playListEditorDigital_t *curElement = (playListEditorDigital_t *)service_element->data;
		if (curElement->service_index != NULL) {
			if((!curElement->data.parent_control) && (curElement->service_index->data.parent_control)) {
				const char *mask = "\\d{6}";
				interface_getText((interfaceMenu_t*)&InterfacePlaylistEditorDigital, _T("ENTER_PASSWORD"), mask, playList_checkParentControlPass, NULL, inputModeDirect, NULL);
				return true;
			}
		}
		service_element = service_element->next;
	}

	push_playlist();
	dvbChannel_applyUpdates();
	return true;
}

int playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
	if (channelNumber == -1) {
		swapMenu = CHANNEL_INFO_GET_CHANNEL(pArg);
		channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

		set_lockColor();
	} else {
		channelNumber = -1;
		set_unLockColor();
		swapMenu = -1;
	}
	interface_displayMenu(1);
	return 0;
}

void playlist_switchElementwithNext(int source){
	list_element_t *curList;
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
		curList = playListEditorDigital;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
		curList = playListEditorAnalog;
	}
	if (curList == NULL)
		return;
	int i = 0;

	list_element_t *rec;
	list_element_t *last = NULL;
	while(curList != NULL) {
		if (i == source){
			if(last == NULL) {
				list_element_t *tmpList;
				tmpList = curList->next;
				curList->next = tmpList->next;
				tmpList->next = curList;
				if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
					playListEditorDigital = tmpList;
				}
				if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {
					playListEditorAnalog = tmpList;
				}
			} else {
				rec = curList->next;
				last->next = rec;
				curList->next = rec->next;
				rec->next = curList;
			}
			break;
		}
		i++;
		last = curList;
		curList = curList->next;
	}
}
void playlist_editor_removeElement()
{
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
	//	curList = playListEditorDigital;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {

	}

}
int enterPlaylistAnalogSelect(interfaceMenu_t *interfaceMenu, void* pArg)
{
	extern list_element_t *bouquetNameAnalogList;
	interfaceMenu_t *channelMenu;
	int i = 0;
	char *name;
	char *bouquets_name;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];

	channelMenu = interfaceMenu;
	interface_clearMenuEntries(channelMenu);

	snprintf(channelEntry, sizeof(channelEntry), "%s: %s", _T("PLAYLIST_NOW_SELECT"), bouquet_getAnalogBouquetName());
	interface_addMenuEntryDisabled(channelMenu, channelEntry, 0);
	interface_addMenuEntry(channelMenu, _T("PLAYLIST_UPDATE_LIST"), bouquet_updateAnalogBouquetList, NULL, settings_interface);
	interface_addMenuEntryDisabled(channelMenu, "List of analog channels:", 0);


	while ((name = bouquet_getNameBouquetList(&bouquetNameAnalogList, i) )!= NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s",i + 1, name);
		interface_addMenuEntry(channelMenu, channelEntry, bouquets_setAnalogBouquet, CHANNEL_INFO_SET(screenMain, i), thumbnail_epg);
		bouquets_name = bouquet_getAnalogBouquetName();
		if (bouquets_name != NULL && strcasecmp(name, bouquets_name) == 0) {
			interfaceMenuEntry_t *entry;
			entry = menu_getLastEntry(channelMenu);
			if(entry) {
				char desc[20];
				snprintf(desc, sizeof(desc), "SELECTED");
				interface_setMenuEntryLabel(entry, desc);
			}
		}
		i++;
	}
	if (i == 0){
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}
	return 0;
}

int enterPlaylistDigitalSelect(interfaceMenu_t *interfaceMenu, void* pArg)
{
	extern list_element_t *bouquetNameDigitalList;
	interfaceMenu_t *channelMenu;
	int i = 0;
	char *name;
	char *bouquets_name;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];

	channelMenu = interfaceMenu;
	interface_clearMenuEntries(channelMenu);

	while ((name = bouquet_getNameBouquetList(&bouquetNameDigitalList, i) )!= NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s",i + 1, name);
		interface_addMenuEntry(channelMenu, channelEntry, bouquets_setDigitalBouquet, CHANNEL_INFO_SET(screenMain, i), thumbnail_epg);
		bouquets_name = bouquet_getDigitalBouquetName();
		if (bouquets_name != NULL && strcasecmp(name, bouquets_name) == 0) {
			interfaceMenuEntry_t *entry;
			entry = menu_getLastEntry(channelMenu);
			if(entry) {
				char desc[20];
				snprintf(desc, sizeof(desc), "SELECTED");
				interface_setMenuEntryLabel(entry, desc);
			}
		}
		i++;
	}
	if (i == 0){
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}
	return 0;

}

int enterPlaylistEditorAnalog(interfaceMenu_t *interfaceMenu, void* pArg)
{
	if (playListEditorAnalog != NULL)
		return 0;

	list_element_t *cur_element = playListEditorAnalog;
	interfaceMenu_t *channelMenu = interfaceMenu;
	playListEditorAnalog_t *element;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;

	interface_clearMenuEntries(channelMenu);
	load_Analog_channels(&cur_element);

	for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
		element = (playListEditorAnalog_t*)cur_element->data;
		if(element == NULL)
			continue;

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ? thumbnail_tvstandard : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.visible ? "VISIBLE" : "INVISIBLE");
			interface_setMenuEntryLabel(entry, desc);
			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		if(appControlInfo.dvbInfo.channel == i) {
			interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
		}
		i++;
	}
	if (i == 0)
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	return 0;
}

int enterPlaylistEditorDigital(interfaceMenu_t *interfaceMenu, void* pArg)
{
	if (playListEditorDigital != NULL)
		return 0;
	list_element_t *cur_element;
	playListEditorDigital_t *element;
	interfaceMenu_t *channelMenu = interfaceMenu;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;
	cur_element = playListEditorDigital;

	load_digital_channels(playListEditorDigital);
	interface_clearMenuEntries(channelMenu);

	for(cur_element = playListEditorDigital ; cur_element != NULL; cur_element = cur_element->next) {
		element = (playListEditorDigital_t*)cur_element->data;
		if(element == NULL)
			continue;


		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ?
								   (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.parent_control ? "PARENT" : (element->data.visible ? "VISIBLE" : "INVISIBLE"));
			interface_setMenuEntryLabel(entry, desc);
			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		if(appControlInfo.dvbInfo.channel == i) {
			interface_setSelectedItem(channelMenu, interface_getMenuEntryCount(channelMenu) - 1);
		}
		i++;
	}
	if (i == 0)
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	return 0;
}