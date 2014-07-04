

#ifdef ENABLE_DVB
/******************************************************************
* INCLUDE FILES                                                   *
*******************************************************************/
#include "playlist_editor.h"
#include "dvbChannel.h"
#include "debug.h"
#include "output.h"
#include "bouquet.h"
#include "analogtv.h"
#include "interface.h"
#include "list.h"
#include "dvb.h"
#include "off_air.h"
#include "l10n.h"
#include "gfx.h"


/******************************************************************
* LOCAL TYPEDEFS                                                  *
*******************************************************************/
typedef struct {
	uint32_t frequency;
	//analog_service_t *service_index;
	service_index_data_t data;
} playListEditorAnalog_t;

typedef struct {
	int32_t               radio;
	int32_t               scrambled;
	service_index_t      *service_index;
	service_index_data_t  data;
	struct list_head      list;
} editorDigital_t;

typedef struct {
	typeBouquet_t  type;
	int32_t        isChanged;
	void          *data;
} playlistEditorMenuParam_t;

/******************************************************************
* GLOBAL DATA                                                     *
*******************************************************************/
interfaceListMenu_t InterfacePlaylistMain;

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/
static int32_t channelNumber = -1;
static list_element_t *playListEditorAnalog = NULL;
static int32_t color_save = -1;
static int32_t swapMenu;
static interfaceListMenu_t InterfacePlaylistAnalog;
static interfaceListMenu_t InterfacePlaylistDigital;
static interfaceListMenu_t InterfacePlaylistSelectDigital;
static interfaceListMenu_t InterfacePlaylistSelectAnalog;
static interfaceListMenu_t InterfacePlaylistEditorDigital;
static interfaceListMenu_t InterfacePlaylistEditorAnalog;

static struct list_head editor_playList = LIST_HEAD_INIT(editor_playList);

static playlistEditorMenuParam_t playlistEditorMenu_digitalParam = {eBouquet_digital, 0, NULL};
static playlistEditorMenuParam_t playlistEditorMenu_analogParam = {eBouquet_analog, 0, NULL};

static int32_t needRefillDigital = 1;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int32_t playlistEditor_enterMainMenu(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistDigital(interfaceMenu_t *pMenu, void* notused);
static int32_t output_enterPlaylistAnalog(interfaceMenu_t *pMenu, void* notused);
static void playList_saveName(typeBouquet_t btype, int32_t num, char *prev_name, char *new_name);
static int32_t get_statusLockPlaylist(void);
static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, typeBouquet_t btype, int32_t count);
// static void playlist_editor_removeElement(void);
// static int32_t getChannelEditor(void);
static void playlistEditor_moveElement(int32_t sourceNum, int32_t move);
static char *output_getSelectedNamePlaylistEditor(void);
// static int32_t enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu);
static int32_t playlistEditor_fillDigital(interfaceMenu_t *interfaceMenu, void *pArg);
static int32_t playlistEditor_save(playlistEditorMenuParam_t *pParam);

// function for editor list
static editorDigital_t *editorList_get(struct list_head *listHead, uint32_t number);
static editorDigital_t *editorList_add(struct list_head *listHead);
static void editorList_release(struct list_head *listHead);

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>_<Word>+    *
*******************************************************************/
void setColor(int32_t color) {
	color_save = color;
}

int32_t getColor() {
	return color_save;
}

static void load_Analog_channels(list_element_t *curListEditor)
{
	extern analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];
	//analogtv_parseConfigFile(1);
	int32_t analogtv_channelCount = analogtv_getChannelCount(1);
	int32_t i;
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

static void load_digital_channels(struct list_head *listHead)
{
	struct list_head *pos;
	editorDigital_t*element;
	if(!listHead) {
		eprintf("%s(): Wrong arguments!\n", __func__);
		return;
	}

	list_for_each(pos, dvbChannel_getSortList()) {
		service_index_t *srvIdx = list_entry(pos, service_index_t, orderNone);

		if(srvIdx == NULL)
			continue;

		element = editorList_add(listHead);

		element->service_index = srvIdx;
		element->data.visible = srvIdx->data.visible;
		element->data.parent_control = srvIdx->data.parent_control;

		if(service_isRadio(srvIdx->service)
			|| (dvb_hasMediaType(srvIdx->service, mediaTypeAudio) && !dvb_hasMediaType(srvIdx->service, mediaTypeVideo)))
		{
			element->radio = 1;
		} else {
			element->radio = 0;
		}
		element->scrambled = dvb_getScrambled(srvIdx->service);
		snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", srvIdx->service->service_descriptor.service_name);
	}
}

static void set_lockColor(void)
{
	setColor(interfaceInfo.highlightColor);

	interfaceInfo.highlightColor++;
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;
}

static void set_unLockColor(void)
{
	interfaceInfo.highlightColor = getColor();
	setColor(-1);
	if (interface_colors[interfaceInfo.highlightColor].A==0)
		interfaceInfo.highlightColor = 0;
}

void playlist_editor_cleanup(typeBouquet_t index)
{
	if(index == eBouquet_all || index == eBouquet_analog) {
		free_elements(&playListEditorAnalog);
	}
}

/*static int32_t getChannelEditor(void)
{
	return channelNumber;
}*/

static int32_t get_statusLockPlaylist(void)
{
	if (channelNumber == -1)
		return false;
	return true;
}

static int32_t enablePlayListEditorMenu(interfaceMenu_t *interfaceMenu)
{
	if((int32_t)interfaceMenu == (int32_t)&InterfacePlaylistEditorDigital.baseMenu) {
		return 1;
	} else if((int32_t)interfaceMenu == (int32_t)&InterfacePlaylistEditorAnalog.baseMenu) {
		return 2;
	}

	return false;
}

static interfaceMenu_t *output_getPlaylistEditorMenu(void)
{
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu))
		return interfaceInfo.currentMenu;
	return NULL;
}

static char *output_getSelectedNamePlaylistEditor(void)
{
	interfaceMenu_t  *baseMenu;
	baseMenu = output_getPlaylistEditorMenu();
	if (baseMenu != NULL)
		return baseMenu->menuEntry[baseMenu->selectedItem].info;
	return NULL;
}

/*static int32_t enablePlayListSelectMenu(interfaceMenu_t *interfaceMenu)
{
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectDigital.baseMenu, sizeof(interfaceListMenu_t)))
		return 1;
	if (!memcmp(interfaceMenu, &InterfacePlaylistSelectAnalog.baseMenu, sizeof(interfaceListMenu_t)))
		return 2;

	return false;
}*/


static void playList_saveName(typeBouquet_t btype, int32_t num, char *prev_name, char *new_name)
{
	int32_t i = 0;
	editorDigital_t *element;

	if(btype == eBouquet_digital) {

		element = editorList_get(&editor_playList, num);
		if(element == NULL) {
			return;
		}

		if(strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))) {
			snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
			return;
		}
		return;
	} else if(btype == eBouquet_analog) {
		list_element_t *cur_element;
		playListEditorAnalog_t *element;
		for(cur_element = playListEditorAnalog; cur_element != NULL; cur_element = cur_element->next) {
			element = (playListEditorAnalog_t*)cur_element->data;
			if(element == NULL)
				continue;
			if(num == i && strncasecmp(element->data.channelsName, prev_name, strlen(prev_name))){
				snprintf(element->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", new_name);
				return;
			}
			i++;
		}
		return;
	}
}

static void playList_nextChannelState(interfaceMenuEntry_t *pMenuEntry, typeBouquet_t btype, int32_t count)
{
	int32_t i = 0;
	list_element_t *cur_element;

	if(btype == eBouquet_digital) {
		editorDigital_t *element;

		element = editorList_get(&editor_playList, count);
		if (element == NULL)
			return;

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
//		interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
		return;
	} else if(btype == eBouquet_analog) {
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
//				interface_changeMenuEntrySelectable(pMenuEntry, element->data.visible);
				return;
			}
			i++;
		}
		return;
	}
}

void merge_digitalLists(typeBouquet_t editorType)
{
	int32_t i = 0;

	if(editorType == eBouquet_digital) {
		struct list_head *pos;
		int32_t i = 0;
		list_for_each(pos, &editor_playList) {
			int32_t first = 0;
			editorDigital_t *element = list_entry(pos, editorDigital_t, list);

			if(element == NULL) {
				continue;
			}

			if(element->service_index != NULL) {
				element->service_index->data.visible        = element->data.visible;
				element->service_index->data.parent_control = element->data.parent_control;
				snprintf(element->service_index->data.channelsName, MENU_ENTRY_INFO_LENGTH, "%s", element->data.channelsName);
			}
			printf("%s[%d] %s\n",__func__, __LINE__, element->service_index->data.channelsName);
			first = dvbChannel_findNumberService(element->service_index);
			if(i > first) {
				dvbChannel_swapServices(first, i);
			} else {
				dvbChannel_swapServices(i, first);
			}

			i++;
		}
	} else if(editorType == eBouquet_analog) {
		list_element_t *service_element = playListEditorAnalog;
		int32_t index;

		extern analog_service_t analogtv_channelParam[MAX_ANALOG_CHANNELS];

		if(service_element == NULL) {
			return;
		}
		while(service_element != NULL) {
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
		bouquet_saveAnalogBouquet();
	}
}

static int32_t playlistEditor_saveInternal(playlistEditorMenuParam_t *pParam)
{
	merge_digitalLists(pParam->type);
	if(pParam->type == eBouquet_digital) {
		dvbChannel_applyUpdates();
	}
	interface_showMessageBox(_T("SAVE"), thumbnail_info, 3000);
	pParam->isChanged = 0;

	return 0;
}

static int32_t playList_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	(void)pMenu;

	if(parentControl_checkPass(value) == 0) {
		playlistEditor_saveInternal(pArg);
	} else {
		interface_showMessageBox(_T("ERR_WRONG_PASSWORD"), thumbnail_error, 3000);
		return 1;
	}

	return 0;
}

static int32_t playlistEditor_save(playlistEditorMenuParam_t *pParam)
{
	if(pParam->type == eBouquet_digital) {
		if(list_empty(&editor_playList)) {
			return -1;
		}

		struct list_head *pos;
		list_for_each(pos, &editor_playList) {
			editorDigital_t *element = list_entry(pos, editorDigital_t, list);
			if(element == NULL) {
				continue;
			}

			if(element->service_index != NULL) {
				if(element->data.parent_control != element->service_index->data.parent_control) {
					const char *mask = "\\d{6}";
					interface_getText((interfaceMenu_t*)&InterfacePlaylistEditorDigital, _T("ENTER_PASSWORD"), mask, playList_checkParentControlPass, NULL, inputModeDirect, (void *)pParam);
					return 0;
				}
			}
		}
		playlistEditor_saveInternal(pParam);
	} else if(pParam->type == eBouquet_analog) {
		//TODO
		if(playListEditorAnalog == NULL) {
			return -1;
		}
	} else {
		eprintf("%s(): Error, unknown playlist editor editorType=%d\n", __func__, pParam->type);
		return -1;
	}
	bouquet_save(pParam->type, bouquet_getCurrentName(pParam->type));

	return 0;
}

int32_t playList_editorChannel(interfaceMenu_t *pMenu, void* pArg)
{
	if(channelNumber == -1) {
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

/*static void playlist_editor_removeElement(void)
{
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 1) {
	//	curList = playListEditorDigital;
	}
	if (enablePlayListEditorMenu(interfaceInfo.currentMenu) == 2) {

	}

}*/


static int32_t playlistEditor_toggleEnable(interfaceMenu_t *pMenu, void *pArg)
{
	bouquet_setEnable(!bouquet_isEnable());
	output_redrawMenu(pMenu);
	return 1;
}

static int32_t bouquet_removeBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	const char *bouquetName;
	bouquetName = bouquet_getCurrentName(eBouquet_digital);
	gfx_stopVideoProvider(screenMain, 1, 1);
	if(bouquetName != NULL) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		bouquet_remove(eBouquet_digital, bouquetName);
		interface_hideMessageBox();
	}

	output_redrawMenu(pMenu);
	//Change selection on "Select playlist", coz this current item became disabled
	interface_setSelectedItem(pMenu, 0);
	return 0;
}

static int32_t bouquet_updateBouquetList(interfaceMenu_t *pMenu, void *pArg)
{
	typeBouquet_t btype = (typeBouquet_t)pArg;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquet_updateNameList(btype, 1);
	interface_hideMessageBox();

	output_redrawMenu(pMenu);
	return 0;
}


static int32_t bouquet_sendBouquetOnServer(interfaceMenu_t *pMenu, void *pArg)
{
	typeBouquet_t btype = (typeBouquet_t)pArg;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	bouquet_upload(btype, bouquet_getCurrentName(btype));
	interface_hideMessageBox();

	return 0;
}

static int32_t bouquet_updateCurent(interfaceMenu_t *pMenu, void *pArg)
{
	const char *bouquetName;
	typeBouquet_t btype = (typeBouquet_t)pArg;

	interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
	gfx_stopVideoProvider(screenMain, 1, 1);

	bouquetName = bouquet_getCurrentName(btype);
	bouquet_update(btype, bouquetName);
	bouquet_open(btype, bouquetName, 1);

	gfx_resumeVideoProvider(screenMain);
	interface_hideMessageBox();

	return 0;
}

static int32_t playlistEditor_createNewBouquet(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	typeBouquet_t btype = pParam->type;

	if((value == NULL) || (strlen(value) <= 0)) {
		interface_showMessageBox(_T("PLAYLIST_WRONG_NAME"), thumbnail_loading, 3000);
		return 0;
	}

	gfx_stopVideoProvider(screenMain, 1, 1);
	if(strList_isExist(bouquet_getNameList(eBouquet_digital), value)) {
		interface_showMessageBox(_T("PLAYLIST_NAME_EXIST"), thumbnail_loading, 0);
		if(!bouquet_isDownloaded(btype, value)) {
			interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
			bouquet_update(btype, value);
		}
	} else {
		bouquet_create(eBouquet_digital, value);
	}

	interface_hideMessageBox();
	if(bouquet_open(btype, value, 0) == 0) {
		if(btype == eBouquet_digital) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistDigital);
		} else if(btype == eBouquet_analog) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistAnalog);
		}
	} else {
		output_redrawMenu(pMenu);
		interface_showMessageBox(_T("PLAYLIST_CANT_OPEN"), thumbnail_error, 3000);
		return -2;
	}
	output_redrawMenu(pMenu);
	return 0;
}


static int32_t playlistEditor_enterNewBouquetNameConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", playlistEditor_createNewBouquet, NULL, inputModeABC, pArg);
			return 1;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t playlistEditor_enterNewBouquetName(interfaceMenu_t *pMenu, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGES_LOSE_ON_CONTINUE"), thumbnail_warning, playlistEditor_enterNewBouquetNameConfirm, pArg);
		return 1;
	}

	interface_getText(pMenu, _T("ENTER_BOUQUET_NAME"), "\\w+", playlistEditor_createNewBouquet, NULL, inputModeABC, pArg);
	return 0;
}

static int32_t playlistEditor_setBouquet(interfaceMenu_t *pMenu, playlistEditorMenuParam_t *pParam)
{
	const char *newBouquetName;
	int32_t number;
	typeBouquet_t btype = pParam->type;

	number = interface_getSelectedItem(pMenu) - 4; //TODO: remove number

	newBouquetName = strList_get(bouquet_getNameList(btype), number);

	if(newBouquetName == NULL) {
		eprintf("ERROR: New bouquet name is NULL!\n", __func__);
		return -1;
	}
	gfx_stopVideoProvider(screenMain, 1, 1);

	if(!bouquet_isDownloaded(btype, newBouquetName)) {
		interface_showMessageBox(_T("PLAYLIST_UPDATE_MESSAGE"), thumbnail_loading, 0);
		bouquet_update(btype, newBouquetName);
		interface_hideMessageBox();
	}

	if(bouquet_open(btype, newBouquetName, 0) == 0) {
		if(btype == eBouquet_digital) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistDigital);
		} else if(btype == eBouquet_analog) {
			interface_menuActionShowMenu(pMenu, &InterfacePlaylistAnalog);
		}
	} else {
		output_redrawMenu(pMenu);
		interface_showMessageBox(_T("PLAYLIST_CANT_OPEN"), thumbnail_error, 3000);
		return -2;
	}

	return 0;
}

static int32_t playlistEditor_changeBouquetConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			playlistEditor_setBouquet(pMenu, (playlistEditorMenuParam_t *)pArg);
			return 0;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t bouquets_setBouquet(interfaceMenu_t *pMenu, void *pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGES_LOSE_ON_CONTINUE"), thumbnail_warning, playlistEditor_changeBouquetConfirm, pArg);
		return 1;
	}

	return playlistEditor_setBouquet(pMenu, pParam);
}

static int32_t playlistEditor_enterNameListMenu(interfaceMenu_t *nameListMenu, void *pArg)
{
	int32_t i = 0;
	const char *name;
	const char *bouquet_name;
	const char *title;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	typeBouquet_t btype = ((playlistEditorMenuParam_t *)pArg)->type;
	
	if(btype == eBouquet_digital) {
		title = _T("PLAYLIST_AVAILABLE_DIGITAL_NAMES");
	} else if(btype == eBouquet_analog) {
		title = _T("PLAYLIST_AVAILABLE_ANALOG_NAMES");
	} else {
		return -1;
	}

	bouquet_name = bouquet_getCurrentName(btype);
	interface_clearMenuEntries(nameListMenu);

	snprintf(channelEntry, sizeof(channelEntry), "%s: %s", _T("PLAYLIST_NOW_SELECT"), bouquet_name ? bouquet_name : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntryDisabled(nameListMenu, channelEntry, 0);
	interface_addMenuEntry(nameListMenu, _T("PLAYLIST_NEW_BOUQUETS"), playlistEditor_enterNewBouquetName, pArg, settings_interface);
	interface_addMenuEntry(nameListMenu, _T("PLAYLIST_UPDATE_LIST"), bouquet_updateBouquetList, pArg, settings_interface);
	interface_addMenuEntryDisabled(nameListMenu, title, 0);

	while((name = strList_get(bouquet_getNameList(btype), i)) != NULL) {
		snprintf(channelEntry, sizeof(channelEntry), "%02d %s", i + 1, name);
		interface_addMenuEntry(nameListMenu, channelEntry, bouquets_setBouquet, pArg, thumbnail_epg);

		if(bouquet_name && (strcasecmp(name, bouquet_name) == 0)) {
			interfaceMenuEntry_t *entry = menu_getLastEntry(nameListMenu);
			if(entry) {
				interface_setMenuEntryLabel(entry, _T("SELECTED"));
			}
		}
		i++;
	}
	if(i == 0) {
		interface_addMenuEntryDisabled(nameListMenu, "NULL", 0);
	}
	return 0;

}

int32_t playlistEditor_enterIntoAnalog(interfaceMenu_t *interfaceMenu, void *pArg)
{
//	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	list_element_t *cur_element = playListEditorAnalog;
	interfaceMenu_t *channelMenu = interfaceMenu;
	playListEditorAnalog_t *element;
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	int32_t i = 0;

	if(playListEditorAnalog != NULL) {
		return 0;
	}

	interface_clearMenuEntries(channelMenu);
	load_Analog_channels(cur_element);// fixed pointer on list

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
//			interface_changeMenuEntrySelectable(entry, element->data.visible);
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

static int32_t playlistEditor_fillDigital(interfaceMenu_t *interfaceMenu, void* pArg)
{
	interfaceMenu_t *channelMenu = interfaceMenu;
	struct list_head *pos;
	int32_t i = 0;
	(void)pArg;

	interface_clearMenuEntries(channelMenu);

	list_for_each(pos, &editor_playList) {
		char channelEntry[MENU_ENTRY_INFO_LENGTH];
		editorDigital_t *element = list_entry(pos, editorDigital_t, list);

		if(element == NULL) {
			continue;
		}

		snprintf(channelEntry, sizeof(channelEntry), "%s. %s", offair_getChannelNumberPrefix(i), element->data.channelsName);
		interface_addMenuEntry(channelMenu, channelEntry, playList_editorChannel, CHANNEL_INFO_SET(screenMain, i), element->data.visible ?
								   (element->scrambled ? thumbnail_billed : (element->radio ? thumbnail_radio : thumbnail_channels)) : thumbnail_not_selected);

		interfaceMenuEntry_t *entry;
		entry = menu_getLastEntry(channelMenu);
		if(entry) {
			char desc[20];
			snprintf(desc, sizeof(desc), "%s", element->data.parent_control ? "PARENT" : (element->data.visible ? "VISIBLE" : "INVISIBLE"));
			interface_setMenuEntryLabel(entry, desc);
//			interface_changeMenuEntrySelectable(entry, element->data.visible);
		}

		i++;
	}
	if(i == 0) {
		interface_addMenuEntryDisabled(channelMenu, "NULL", 0);
	}

	return 0;
}

int32_t playlistEditor_enterIntoDigital(interfaceMenu_t *interfaceMenu, void *pArg)
{
// 	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;
	if(needRefillDigital == 0) {
		return 0;
	}

	editorList_release(&editor_playList);
	load_digital_channels(&editor_playList);
	playlistEditor_fillDigital(interfaceMenu, pArg);
	needRefillDigital = 0;

	if((appControlInfo.dvbInfo.channel >= 0) && (appControlInfo.dvbInfo.channel < interface_getMenuEntryCount(interfaceMenu))) {
		interface_setSelectedItem(interfaceMenu, appControlInfo.dvbInfo.channel);
	}

	return 0;
}


static int32_t playlistEditor_wantSaveConfirm(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommandGreen:
		case interfaceCommandEnter:
		case interfaceCommandOk:
			playlistEditor_save(pArg);
			return 0;
			break;
		case interfaceCommandRed:
		case interfaceCommandExit:
		case interfaceCommandLeft:
		default:
			return 0;
			break;
	}
	return 1;
}

static int32_t playlistEditor_onExit(interfaceMenu_t *interfaceMenu, void* pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	if(pParam->isChanged) {
		interface_showConfirmationBox(_T("PLAYLIST_CHANGED_DO_YOU_WANT_SAVE"), thumbnail_warning, playlistEditor_wantSaveConfirm, pArg);
	}

	return 0;
}

static int32_t playlistEditor_saveParentControlPass(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	if(parentControl_savePass(value) != 0) {
		interface_showMessageBox(_T("PARENTCONTROL_CANT_SAVE_PASS"), thumbnail_error, 3000);
		return -1;
	}
	return 0;
}

static int32_t playlistEditor_checkParentControlPass(interfaceMenu_t *pMenu, char *value, void *pArg)
{
	if(parentControl_checkPass(value) == 0) {
		const char *mask = "\\d{6}";
		interface_getText(pMenu, _T("PARENTCONTROL_ENTER_NEW_PASSWORD"), mask, playlistEditor_saveParentControlPass, NULL, inputModeDirect, pArg);
		return 1;
	}

	return 0;
}

static int32_t playlistEditor_changeParentControlPass(interfaceMenu_t* pMenu, void* pArg)
{
	const char *mask = "\\d{6}";
	interface_getText(pMenu, _T("PARENTCONTROL_CHECK_PASSWORD"), mask, playlistEditor_checkParentControlPass, NULL, inputModeDirect, pArg);
	return 0;
}

static int32_t playlistEditor_enterMainMenu(interfaceMenu_t *interfaceMenu, void* notused)
{
	char str[MENU_ENTRY_INFO_LENGTH];
	int32_t enabled = bouquet_isEnable();
	interface_clearMenuEntries(interfaceMenu);
	snprintf(str, sizeof(str), "%s: %s", _T("PLAYLIST_ENABLE"), enabled ? "ON" : "OFF");
	interface_addMenuEntry(interfaceMenu, str, playlistEditor_toggleEnable, NULL, settings_interface);
	
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_ANALOG"), enabled, interface_menuActionShowMenu, &InterfacePlaylistAnalog, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_DIGITAL"), enabled, interface_menuActionShowMenu, &InterfacePlaylistDigital, settings_interface);
	return 0;
}

static int32_t output_enterPlaylistAnalog(interfaceMenu_t *interfaceMenu, void* notused)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *bouquetName;
	int32_t enabled;

	interface_clearMenuEntries(interfaceMenu);
	bouquetName = bouquet_getCurrentName(eBouquet_analog);
	enabled = bouquetName ? 1 : 0;

	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquetName ? bouquetName : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectAnalog, settings_interface);

	interface_addMenuEntry(interfaceMenu, _T("PLAYLIST_EDITOR"), interface_menuActionShowMenu, &InterfacePlaylistEditorAnalog, settings_interface);	

	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_UPDATE"), enabled, bouquet_updateCurent, (void *)eBouquet_analog, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), enabled, bouquet_sendBouquetOnServer, (void *)eBouquet_analog, settings_interface);

	return 0;
}

static int32_t output_enterPlaylistDigital(interfaceMenu_t *interfaceMenu, void *pArg)
{
	char buf[MENU_ENTRY_INFO_LENGTH];
	const char *bouquet_name;
	int32_t enabled;
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pArg;

	interface_clearMenuEntries(interfaceMenu);

	bouquet_name = bouquet_getCurrentName(eBouquet_digital);
	enabled = bouquet_name ? 1 : 0;

	snprintf(buf, sizeof(buf), "%s: %s", _T("PLAYLIST_SELECT"), bouquet_name ? bouquet_name : _T("PLAYLIST_NOT_SELECTED"));
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistSelectDigital, settings_interface);

	if(pParam->isChanged) {
		snprintf(buf, sizeof(buf), "%s (%s)", _T("PLAYLIST_EDITOR"), _T("PLAYLIST_MDIFIED"));
	} else {
		snprintf(buf, sizeof(buf), "%s", _T("PLAYLIST_EDITOR"));
	}
	interface_addMenuEntry(interfaceMenu, buf, interface_menuActionShowMenu, &InterfacePlaylistEditorDigital, settings_interface);

	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_UPDATE"), enabled, bouquet_updateCurent, (void *)eBouquet_digital, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_SAVE_BOUQUETS"), enabled, bouquet_sendBouquetOnServer, (void *)eBouquet_digital, settings_interface);
	interface_addMenuEntry2(interfaceMenu, _T("PLAYLIST_REMOVE"), enabled, bouquet_removeBouquet, NULL, settings_interface);

	interface_addMenuEntry(interfaceMenu, _T("PARENTCONTROL_CHANGE"), playlistEditor_changeParentControlPass, NULL, settings_interface);
	return 0;
}

static char* interface_getChannelCaption(int32_t dummy, void* pArg)
{
	(void)dummy;
	char * ptr = strstr(output_getSelectedNamePlaylistEditor(), ". ");
	if (ptr) {
		ptr += 2;
		return ptr;
	}
	return NULL;
}

static int32_t interface_saveChannelCaption(interfaceMenu_t *pMenu, char *pStr, void* pArg)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pMenu->pArg;
	if(pStr == NULL) {
		return -1;
	}
	playList_saveName(pParam->type, pMenu->selectedItem, pMenu->menuEntry[pMenu->selectedItem].info, pStr);
	snprintf(pMenu->menuEntry[pMenu->selectedItem].info, MENU_ENTRY_INFO_LENGTH, "%s. %s", offair_getChannelNumberPrefix(pMenu->selectedItem), pStr);
	pParam->isChanged = 1;
/*
    if((DVBTMenu.baseMenu.selectedItem - 2) < dvbChannel_getCount()) {
        EIT_service_t *service;
        int32_t channelNumber;

        channelNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
        service = dvbChannel_getService(channelNumber);
        if(service == NULL) {
            return -1;
        }

        snprintf((char *)service->service_descriptor.service_name, MENU_ENTRY_INFO_LENGTH, "%s", pStr);

        // todo : save new caption to config file
	} else if(DVBTMenu.baseMenu.selectedItem < (int32_t)(dvbChannel_getCount() + analogtv_getChannelCount(0) + 3)) {
        uint32_t selectedItem = DVBTMenu.baseMenu.selectedItem - dvbChannel_getCount() - 3;
        analogtv_updateName(selectedItem, pStr);
    }

#warning "Wrong printing number for analog TV!"
    snprintf(DVBTMenu.baseMenu.menuEntry[DVBTMenu.baseMenu.selectedItem].info,
              MENU_ENTRY_INFO_LENGTH, "%02d. %s", DVBTMenu.baseMenu.selectedItem - 2, pStr);
*/
	return 0;
}

static void playlistEditor_moveElement(int32_t elementId, int32_t move)
{
	struct list_head *el;
	int32_t i = 0;

	dprintf("%s[%d] elementId=%d\n", __func__, __LINE__, elementId);

	list_for_each(el, &editor_playList) {
		editorDigital_t *secElement = list_entry(el, editorDigital_t, list);
		if(secElement == NULL) {
			continue;
		}

		if(i == elementId) {
			struct list_head *newPos = el->prev;
			list_del(el);
			while(move != 0) {
				if(move > 0) {
					newPos = newPos->next;
					move--;
				} else {
					newPos = newPos->prev;
					move++;
				}
				if(newPos == &editor_playList) {
//					eprintf("%s(): Warning, something wrong!!!\n", __func__);
					break;
				}
			}
			list_add(el, newPos);
			break;
		}
		i++;
	}

}

static int32_t interface_switchMenuEntryCustom(interfaceMenu_t *pMenu, int32_t srcId, int32_t move)
{
// 	int32_t dstId;
// 	interfaceMenuEntry_t cur;

	if(move == 0) {
		return 0;
	} else if(move > 0) {
		int32_t menuEntryCount = interface_getMenuEntryCount(pMenu);
		if(srcId >= (menuEntryCount - 1)) {
			return -1;
		}
		if((srcId + move) >= menuEntryCount) {
			move = menuEntryCount - srcId - 1;
		}
	} else {
		if(srcId <= 0) {
			return -1;
		}
		if((srcId + move) < 0) {
			move = -srcId;
		}
	}

//	dstId = srcId + move;
	playlistEditor_moveElement(srcId, move);

	playlistEditor_fillDigital(pMenu, NULL);

	//TODO: Here we can directly rename menu items instead of refilling full menu by calling playlistEditor_fillDigital()!!!
// 	memcpy(&cur, &pMenu->menuEntry[srcId], sizeof(interfaceMenuEntry_t));
// 	memcpy(&pMenu->menuEntry[srcId], &pMenu->menuEntry[dstId], sizeof(interfaceMenuEntry_t));
// 	memcpy(&pMenu->menuEntry[dstId], &cur, sizeof(interfaceMenuEntry_t));
// 
// 	snprintf(pMenu->menuEntry[srcId].info, MENU_ENTRY_INFO_LENGTH, "%s", offair_getChannelNumberPrefix(srcId));
// 	snprintf(pMenu->menuEntry[dstId].info, MENU_ENTRY_INFO_LENGTH, "%s", offair_getChannelNumberPrefix(dstId));

// 	pMenu->menuEntry[dstId].pArg = pMenu->menuEntry[srcId].pArg;
// 	pMenu->menuEntry[srcId].pArg = cur.pArg;

	return 0;
}

static int32_t playlistEditor_processCommand(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd)
{
	playlistEditorMenuParam_t *pParam = (playlistEditorMenuParam_t *)pMenu->pArg;

	if(get_statusLockPlaylist()) {
		switch(cmd->command) {
// 			case interfaceCommandLeft:
// 				if(curItem >= 0 && curItem < interface_getMenuEntryCount(pMenu)) {
// 					playlist_editor_removeElement(curItem);
// 				}
// 
// 				interface_displayMenu(1);
// 				return 1;
			case interfaceCommandUp:
			case interfaceCommandDown:
			case interfaceCommandPageUp:
			case interfaceCommandPageDown:
			{
				int32_t itemHeight;
				int32_t maxVisibleItems;
				interface_listMenuGetItemInfo((interfaceListMenu_t *)pMenu, &itemHeight, &maxVisibleItems);

				table_IntInt_t moves[] = {
					{interfaceCommandUp,       -1},
					{interfaceCommandDown,      1},
					{interfaceCommandPageUp,   -maxVisibleItems},
					{interfaceCommandPageDown,  maxVisibleItems},
					TABLE_INT_INT_END_VALUE
				};

				if(interface_switchMenuEntryCustom(pMenu, pMenu->selectedItem, table_IntIntLookup(moves, cmd->command, 0)) != 0) {
					return 1;
				}
				pParam->isChanged = 1;
				break;
			}
			case interfaceCommandEnter:
			case interfaceCommandOk:
				break;
			default:
				return 0;
		}
	} else {
		if(cmd->command == interfaceCommandYellow) {
			interface_getText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", interface_saveChannelCaption, interface_getChannelCaption, inputModeABC, pMenu->pArg);
			return 0;
		}
	}

	if(cmd->command == interfaceCommandGreen) {
		playlistEditor_save(pMenu->pArg);
		return 0;
	} else if(cmd->command == interfaceCommandRight) {
		int32_t n = pMenu->selectedItem;
		if((n >= 0) && (n < interface_getMenuEntryCount(pMenu))) {
			playList_nextChannelState(&pMenu->menuEntry[n], pParam->type, n);
			interface_displayMenu(1);
			pParam->isChanged = 1;
			return 1;
		}
	}

	return interface_listMenuProcessCommand(pMenu, cmd);
}


static int32_t playlistEditor_dvbChannelsChangeCallback(void *pArg)
{
	(void)pArg;
	needRefillDigital = 1;
	playlistEditorMenu_digitalParam.isChanged = 0;
	return 0;
}


int32_t playlistEditor_init(void)
{
	int32_t playlistEditor_icons[4] = { statusbar_f1_cancel, statusbar_f2_ok, statusbar_f3_edit, 0};

	createListMenu(&InterfacePlaylistMain, _T("PLAYLIST_MAIN"), settings_interface, NULL, _M &OutputMenu,
		interfaceListMenuIconThumbnail, playlistEditor_enterMainMenu, NULL, NULL);

	createListMenu(&InterfacePlaylistAnalog, _T("PLAYLIST_ANALOG"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistAnalog, NULL, (void *)&playlistEditorMenu_analogParam);

	createListMenu(&InterfacePlaylistDigital, _T("PLAYLIST_DIGITAL"), settings_interface, NULL, _M &InterfacePlaylistMain,
		interfaceListMenuIconThumbnail, output_enterPlaylistDigital, NULL, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistSelectAnalog, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, playlistEditor_enterNameListMenu, NULL, (void *)&playlistEditorMenu_analogParam);

	createListMenu(&InterfacePlaylistSelectDigital, _T("PLAYLIST_SELECT"), settings_interface, NULL, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, playlistEditor_enterNameListMenu, NULL, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistEditorDigital, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistDigital,
		interfaceListMenuIconThumbnail, playlistEditor_enterIntoDigital, playlistEditor_onExit, (void *)&playlistEditorMenu_digitalParam);

	createListMenu(&InterfacePlaylistEditorAnalog, _T("PLAYLIST_EDITOR"), settings_interface, playlistEditor_icons, _M &InterfacePlaylistAnalog,
		interfaceListMenuIconThumbnail, playlistEditor_enterIntoAnalog, playlistEditor_onExit, (void *)&playlistEditorMenu_analogParam);

	InterfacePlaylistEditorDigital.baseMenu.processCommand = playlistEditor_processCommand;
	InterfacePlaylistEditorAnalog.baseMenu.processCommand = playlistEditor_processCommand;

	//Register callback on dvbChannels chanched
	dvbChannel_registerCallbackOnChange(playlistEditor_dvbChannelsChangeCallback, NULL);

	return 0;
}

int32_t playlistEditor_terminate(void)
{
	editorList_release(&editor_playList);
	playlist_editor_cleanup(eBouquet_all);
	return 0;
}



editorDigital_t *editorList_add(struct list_head *listHead)
{
	editorDigital_t *element;
	element = malloc(sizeof(editorDigital_t));
	if(!element) {
		eprintf("%s(): Allocation error!\n", __func__);
		return NULL;
	}
	list_add_tail(&element->list, listHead);

	return element;
}

void editorList_release(struct list_head *listHead)
{
	struct list_head *pos;
	struct list_head *n;

	list_for_each_safe(pos, n, listHead) {
		editorDigital_t *el = list_entry(pos, editorDigital_t, list);

		list_del(&el->list);
		free(el);
	}
}

editorDigital_t *editorList_get(struct list_head *listHead, uint32_t number)
{
	struct list_head *pos;
	uint32_t id = 0;
	if(!listHead) {
		eprintf("%s(): Wrong argument!\n", __func__);
		return NULL;
	}
	list_for_each(pos, listHead) {
		if(id == number) {
			editorDigital_t *el = list_entry(pos, editorDigital_t, list);
			return el;
		}
		id++;
	}
	return NULL;
}

#endif //#ifdef ENABLE_DVB
