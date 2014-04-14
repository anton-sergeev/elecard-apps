

/*
 analogtv.c

Copyright (C) 2013  Elecard Devices

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Elecard Devices nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ELECARD DEVICES BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "analogtv.h"

#ifdef ENABLE_ANALOGTV

#include "debug.h"
#include "app_info.h"
#include "StbMainApp.h"
#include "interface.h"
#include "l10n.h"
#include "playlist.h"
#include "sem.h"
#include "stsdk.h"
#include "gfx.h"
#include "off_air.h"

#include <cJSON.h>
#include <sys/stat.h>

/***********************************************
* LOCAL MACROS                                 *
************************************************/

#define fatal   eprintf
#define info    dprintf
#define verbose(...)
#define debug(...)

#define PERROR(fmt, ...)		eprintf(fmt " (%s)\n", ##__VA_ARGS__, strerror(errno))

#define FILE_PERMS				(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

#define ANALOGTV_CHANNEL_FILE	CONFIG_DIR "/analog.conf"
#define ANALOGTV_CONFIG_JSON	CONFIG_DIR "/analog.json"

#define ANALOGTV_UNDEF			"UNDEF"
#define MAX_ANALOG_CHANNELS		128

#define TV_STATION_FULL_LIST	"/tmp/tvchannels.txt"

#define MAX_SERVICE_COUNT 2048

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/
typedef struct {
	uint32_t frequency;
	uint16_t customNumber;
	char customCaption[256];
	char sysEncode[16];
	char audio[16];
} analog_service_t;

typedef struct _short_chinfo {
	char name[50];
	uint32_t id;
} short_chinfo;

typedef struct _full_chinfo {
	char name[50];
	uint32_t id;
	uint32_t freq;
} full_chinfo;

/***********************************************
* EXPORTED DATA                                *
************************************************/

analog_service_t 	analogtv_channelParam[MAX_ANALOG_CHANNELS];

static uint32_t		analogtv_channelCount = 0;

static interfaceListMenu_t AnalogTVChannelMenu;
static short_chinfo *full_service_list = NULL;//[MAX_SERVICE_COUNT];
static full_chinfo *found_service_list = NULL;//[MAX_SERVICE_COUNT];
static uint32_t found_service_count = 0;
static uint32_t full_service_count = 0;
char channel_names_file_full[256];
int8_t services_edit_able = 0;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

/******************************************************************
* STATIC DATA                                                     *
*******************************************************************/
static pmysem_t analogtv_semaphore;

/******************************************************************
* FUNCTION DECLARATION                     <Module>[_<Word>+]  *
*******************************************************************/

/******************************************************************
* FUNCTION IMPLEMENTATION                     <Module>[_<Word>+]  *
*******************************************************************/

int analogtv_clearServiceList(interfaceMenu_t * pMenu, void *pArg)
{
	int permanent = (int)pArg;
	mysem_get(analogtv_semaphore);
	analogtv_channelCount = 0;
	mysem_release(analogtv_semaphore);
	appControlInfo.offairInfo.previousChannel = 0;
	saveAppSettings();

	remove(ANALOGTV_CONFIG_JSON);
	if (permanent > 0) remove(appControlInfo.tvInfo.channelConfigFile);

	pMenu->pActivatedAction(pMenu, pMenu->pArg);
	interface_displayMenu(1);

	offair_fillDVBTMenu();

	return 0;
}

static int32_t analogtv_parseOldConfigFile(void)
{
	int i = 0;
	FILE *fd = NULL;

	fd = fopen(ANALOGTV_CHANNEL_FILE, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CHANNEL_FILE);
		return -1;
	}
	while(!feof(fd)) {
		uint32_t freq;
		char buf[256];
		const char *name;

		fgets(buf, sizeof(buf), fd);
		freq = strtoul(buf, NULL, 10);

		name = strchr(buf, ';');
		if(name) {
			name++;
		} else {
			snprintf(buf, sizeof(buf), "TV Program %02d", i + 1);
			name = buf;
		}

		analogtv_channelParam[i].frequency = freq;
		strncpy(analogtv_channelParam[i].customCaption, name, sizeof(analogtv_channelParam[0].customCaption));
		strncpy(analogtv_channelParam[i].sysEncode, ANALOGTV_UNDEF, sizeof(analogtv_channelParam[0].sysEncode));
		i++;
	}
	analogtv_channelCount = i;
	fclose(fd);
	return 0;
}

/*static int32_t analogtv_saveOldConfigFile(void)
{
	int32_t i;
	FILE *fd = NULL;

	fd = fopen(ANALOGTV_CHANNEL_FILE, "w");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CHANNEL_FILE);
		return -1;
	}

	for(int i = 0; i < (int32_t)analogtv_channelCount; i++) {
		fprintf(fd, "%u;%s\n", analogtv_channelParam[i].frequency, analogtv_channelParam[i].customCaption);
	}

	fclose(fd);
	return 0;
}*/

static int32_t analogtv_parseConfigFile(void)
{
	FILE *fd = NULL;
	cJSON *root;
	cJSON *format;
	char *data;
	long len;

	fd = fopen(ANALOGTV_CONFIG_JSON, "r");
	if(fd == NULL) {
		dprintf("Error opening %s\n", ANALOGTV_CONFIG_JSON);
		//Is this need still
		return analogtv_parseOldConfigFile();
	}
	fseek(fd, 0, SEEK_END);
	len = ftell(fd);
	fseek(fd, 0, SEEK_SET);
	data = malloc(len + 1);
	fread(data, 1, len, fd);
	fclose(fd);

	root = cJSON_Parse(data);
	free(data);
	if(!root) {
		printf("Error before: [%s]\n", cJSON_GetErrorPtr());
		return -1;
	}

	format = cJSON_GetObjectItem(root, "Analog TV channels");
	if(format) {
		uint32_t i;
		analogtv_channelCount = cJSON_GetArraySize(format);
		for(i = 0 ; i < analogtv_channelCount; i++) {
			cJSON *subitem = cJSON_GetArrayItem(format, i);
			if(subitem) {
				analogtv_channelParam[i].frequency = objGetInt(subitem, "frequency", 0);
				strncpy(analogtv_channelParam[i].customCaption, objGetString(subitem, "name", ""), sizeof(analogtv_channelParam[0].customCaption));
				strncpy(analogtv_channelParam[i].sysEncode, objGetString(subitem, "system encode", ANALOGTV_UNDEF), sizeof(analogtv_channelParam[0].sysEncode));
				strncpy(analogtv_channelParam[i].audio, objGetString(subitem, "audio demod mode", ANALOGTV_UNDEF), sizeof(analogtv_channelParam[0].audio));
				if(analogtv_channelParam[i].customCaption[0] == 0) {
					sprintf(analogtv_channelParam[i].customCaption, "TV Program %02d", i + 1);
				}
			}
		}
	}
	cJSON_Delete(root);

	return 0;
}

static int32_t analogtv_saveConfigFile(void)
{
	cJSON* root;
	cJSON* format;
	char *rendered;
	uint32_t i;

	root = cJSON_CreateObject();
	if(!root) {
		dprintf("Memory error!\n");
		return -1;
	}
	format = cJSON_CreateArray();
	if(!format) {
		dprintf("Memory error!\n");
		cJSON_Delete(root);
		return -1;
	}
	cJSON_AddItemToObject(root, "Analog TV channels", format);

	for(i = 0; i < analogtv_channelCount; i++) {
		cJSON* fld;

		fld = cJSON_CreateObject();
		if(fld) {
			cJSON_AddNumberToObject(fld, "id", i + 1);
			cJSON_AddNumberToObject(fld, "frequency", analogtv_channelParam[i].frequency);
			cJSON_AddStringToObject(fld, "name", analogtv_channelParam[i].customCaption);
			cJSON_AddStringToObject(fld, "system encode", analogtv_channelParam[i].sysEncode);
			cJSON_AddStringToObject(fld, "audio demod mode", analogtv_channelParam[i].audio);

			cJSON_AddItemToArray(format, fld);
		} else {
			dprintf("Memory error!\n");
		}
	}

	rendered = cJSON_Print(root);
	cJSON_Delete(root);

	if(rendered) {
		FILE *fd = NULL;
		fd = fopen(ANALOGTV_CONFIG_JSON, "w");
		if(fd) {
			fwrite(rendered, strlen(rendered), 1, fd);
			fclose(fd);
		} else {
			dprintf("Error opening %s\n", ANALOGTV_CONFIG_JSON);
//			return -1;
		}
		free(rendered);
	}
	return 0;
}

int32_t analogtv_updateName(uint32_t chanIndex, char* str)
{
	if(!str) {
		dprintf("%s(): Wrong name\n", __func__);
		return -1;
	}
	if(chanIndex >= analogtv_channelCount) {
		dprintf("%s(): Wrong index\n", __func__);
		return -2;
	}

	strncpy(analogtv_channelParam[chanIndex].customCaption, str, sizeof(analogtv_channelParam[0].customCaption));
	return analogtv_saveConfigFile();
}


// int analogtv_readServicesFromFile ()
// {
// 	if (!helperFileExists(appControlInfo.tvInfo.channelConfigFile)) return -1;
//
// 	int res = 0;
// 	analogtv_clearServiceList(NULL, 0);
//
// 	/// TODO : read from XML file and set analogtv_channelCount
//
// 	eprintf("%s: loaded %d services\n", __FUNCTION__, analogtv_channelCount);
//
// 	return res;
// }

#define RPC_ANALOG_SCAN_TIMEOUT      (180)

int32_t analogtv_updateFoundServiceFile(void)
{
	FILE *file = fopen(channel_names_file_full, "w");
	if((file) && (found_service_list)) {
		uint32_t i;
		for(i = 0; i < found_service_count; i++) {
			fprintf(file ,"%d %d %s\n", found_service_list[i].freq/1000000, found_service_list[i].id, found_service_list[i].name);
		}
		fclose(file);
	}
	return 0;
}

static int32_t analogtv_renameFromList(interfaceMenu_t *pMenu, void* pArg)
{
	uint32_t i;
	uint8_t in_list = 0;
	
	if(full_service_list == NULL) {
		dprintf("%s(): Incorrect data in channel list\n", __func__);
		return -1;
	}

	//rename service and update menu list
	analogtv_updateName(appControlInfo.tvInfo.id, full_service_list[AnalogTVChannelMenu.baseMenu.selectedItem].name);
	offair_fillDVBTMenu();

	//hide menu and activate menu of all channels
	interface_showMenu(0, 1);
	offair_activateChannelMenu();

	//update list of renaming channels
	for(i = 0; i < found_service_count; i++) {
		if(found_service_list[i].freq == analogtv_channelParam[appControlInfo.tvInfo.id].frequency) {
			in_list = 1;
			break;
		}
	}

	if(in_list) {	//if this channel exist in list
		found_service_list[i].id = full_service_list[AnalogTVChannelMenu.baseMenu.selectedItem].id;
		strncpy(found_service_list[i].name, full_service_list[AnalogTVChannelMenu.baseMenu.selectedItem].name, sizeof(found_service_list[i].name));
	}
	else {		//if this channel not exist in list, then add it to list
		full_chinfo found_service_list_temp[MAX_SERVICE_COUNT];
		if(found_service_list != NULL) {
			memcpy(found_service_list_temp, found_service_list, found_service_count*sizeof(full_chinfo));
			free(found_service_list);
		}

		found_service_list_temp[found_service_count].id = full_service_list[AnalogTVChannelMenu.baseMenu.selectedItem].id;
		strncpy(	found_service_list_temp[found_service_count].name, 
			full_service_list[AnalogTVChannelMenu.baseMenu.selectedItem].name,
			sizeof(found_service_list_temp[found_service_count].name));
		found_service_list_temp[found_service_count].freq = analogtv_channelParam[appControlInfo.tvInfo.id].frequency;
		found_service_count++;

		found_service_list = malloc(found_service_count*sizeof(full_chinfo));
		memcpy(found_service_list, found_service_list_temp, found_service_count*sizeof(full_chinfo));
	}

	//save renaming channel list to file
	analogtv_updateFoundServiceFile();

	return 0;
}

static int32_t analogtv_fillFullServList()
{
	FILE *file;
	char chname[50];
	uint32_t chid;
	
	if(full_service_list != NULL) {
		free(full_service_list);
	}

	//read full channel list from file
	file = fopen(TV_STATION_FULL_LIST, "r");
	full_service_count = 0;
	short_chinfo full_service_list_temp[MAX_SERVICE_COUNT];

	if (file!=NULL) {
		while(!feof(file)) {
		      if(fscanf(file, "%d ", &chid) > 0) {
			      fgets(chname, sizeof(chname), file);
			      if(chname[strlen(chname)-1] == '\n') {
				      chname[strlen(chname)-2] = '\0';
			      }

			      strncpy(full_service_list_temp[full_service_count].name, chname, sizeof(full_service_list_temp[full_service_count].name));
			      full_service_list_temp[full_service_count].id = chid;

			      full_service_count++;
			      if (full_service_count > MAX_SERVICE_COUNT) {
				      full_service_count = MAX_SERVICE_COUNT;
				      break;
			      }
		      }
		}
		fclose(file);
		full_service_list = malloc(full_service_count*sizeof(short_chinfo));
		memcpy(full_service_list, full_service_list_temp, full_service_count*sizeof(short_chinfo));
	}

	return 0;
}

int32_t analogtv_fillFoundServList()
{
	FILE *file;
	//read already renaming channels from file
	file = fopen(channel_names_file_full, "r");
	found_service_count = 0;
	full_chinfo found_service_list_temp[MAX_SERVICE_COUNT];

	if(found_service_list != NULL) {
		free(found_service_list);
	}

	if (file!=NULL) {
		while(!feof(file)) {
		      if(fscanf(file, "%d %d ", &found_service_list_temp[found_service_count].freq, &found_service_list_temp[found_service_count].id) > 0) {
			      found_service_list_temp[found_service_count].freq *= 1000000;
			      fgets(found_service_list_temp[found_service_count].name, sizeof(found_service_list_temp[found_service_count].name), file);
			      if(found_service_list_temp[found_service_count].name[strlen(found_service_list_temp[found_service_count].name)-1] == '\n') {
				      found_service_list_temp[found_service_count].name[strlen(found_service_list_temp[found_service_count].name)-1] = '\0';
			      }

			      found_service_count++;
			      if (found_service_count > MAX_SERVICE_COUNT) {
				      found_service_count = MAX_SERVICE_COUNT;
				      break;
			      }
		      }
		}
		fclose(file);
		found_service_list = malloc(found_service_count*sizeof(full_chinfo));
		memcpy(found_service_list, found_service_list_temp, found_service_count*sizeof(full_chinfo));
	}

	return 0;
}

static int32_t analogtv_fillServiceNamesMenu(short_chinfo *list, int32_t list_count)
{
	interfaceMenu_t *tvMenu = _M &AnalogTVChannelMenu;
	char buf[256];

	if((list == NULL) || (list_count < 0)) {
		dprintf("Error filling menu\n");
		return -1;
	}

	interface_clearMenuEntries(tvMenu);
	for(int i = 0; i < list_count; i++) {
		snprintf(buf, sizeof(buf), "%d. %s", list[i].id, list[i].name);
		interface_addMenuEntry(tvMenu, buf, analogtv_renameFromList, NULL, thumbnail_channels);
	}
	AnalogTVChannelMenu.baseMenu.selectedItem = 0;

	return 0;
}

static int32_t analogtv_confInit()
{
	services_edit_able = 0;
	sprintf(channel_names_file_full, "/tmp/%s.txt", appControlInfo.tvInfo.channelNamesFile);

	return 0;
}

static int32_t analogtv_checkServiceNames()
{
	if(found_service_list == NULL) {
		dprintf("%s(): Incorrect data in founded channel list\n", __func__);
		return -1;
	}

	for(uint32_t i = 0; i < analogtv_channelCount; i++) {
		for(uint32_t j = 0; j < found_service_count; j++) {
			if(found_service_list[j].freq == analogtv_channelParam[i].frequency) {
				analogtv_updateName(i, found_service_list[j].name);
				break;
			}
		}
	}
	offair_fillDVBTMenu();

	return 0;
}

static int32_t analogtv_menuServicesShow()
{
	interfaceMenu_t *tvMenu = _M &AnalogTVChannelMenu;
	analogtv_fillServiceNamesMenu(full_service_list, full_service_count);
	interface_menuActionShowMenu(tvMenu, tvMenu);
	interface_showMenu(1, 1);

	return 0;
}

static int32_t analogtv_findServicesInList(interfaceMenu_t *pMenu, char* pStr, void* pArg)
{
	int32_t new_service_count = 0;
	uint32_t i;
	(void)pArg;
	if (pStr == NULL) {
		return 0;
	}

	short_chinfo new_service_list[MAX_SERVICE_COUNT];

	for(i = 0; i < full_service_count; i++) {
		if(strncasecmp(full_service_list[i].name, pStr, strlen(pStr)) == 0) {
			strncpy(new_service_list[new_service_count].name, full_service_list[i].name, sizeof(new_service_list[new_service_count].name));
			new_service_list[new_service_count].id = full_service_list[i].id;
			new_service_count++;
		}
	}

	full_service_count = new_service_count;

	if(full_service_list != NULL) {
		free(full_service_list);
	}

	full_service_list = malloc(full_service_count*sizeof(short_chinfo));
	memcpy(full_service_list, new_service_list, full_service_count*sizeof(short_chinfo));

	analogtv_fillServiceNamesMenu(full_service_list, full_service_count);

	return 0;
}

static int32_t analogtv_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	switch( cmd->command )
	{
		case interfaceCommandBlue:
			if(services_edit_able) {
				analogtv_fillFullServList();
				analogtv_fillServiceNamesMenu(full_service_list, full_service_count);
				interface_showMenu(1, 1);
				return 0;
			}
		case interfaceCommandGreen:
			if(services_edit_able) {
				interface_getText(pMenu, _T("DVB_ENTER_CAPTION"), "\\w+", analogtv_findServicesInList, NULL, inputModeABC, pArg);
				return 0;
			}
		default: ;
	}

	return 1;
}

int analogtv_serviceScan(interfaceMenu_t *pMenu, void* pArg)
{
#ifdef STSDK
	char buf[256];
	uint32_t from_freq, to_freq;

	sprintf(buf, "%s", _T("SCANNING_ANALOG_CHANNELS"));
	interface_showMessageBox(buf, thumbnail_info, 0);
	
	from_freq = appControlInfo.tvInfo.lowFrequency * KHZ;
	to_freq = appControlInfo.tvInfo.highFrequency * KHZ;

	cJSON *params = cJSON_CreateObject();
	cJSON *result = NULL;
	elcdRpcType_t type = elcdRpcInvalid;

	if(!params) {
		eprintf("%s: out of memory\n", __FUNCTION__);
		return -1;
	}

	cJSON_AddItemToObject(params, "from_freq", cJSON_CreateNumber(from_freq));
	cJSON_AddItemToObject(params, "to_freq", cJSON_CreateNumber(to_freq));

	char *analogtv_delSysName[] = {
		[TV_SYSTEM_PAL]		= "pal",
		[TV_SYSTEM_SECAM]	= "secam",
		[TV_SYSTEM_NTSC]	= "ntsc",
	};
	cJSON_AddItemToObject(params, "delsys", cJSON_CreateString(analogtv_delSysName[appControlInfo.tvInfo.delSys]));

	char *analogtv_audioName[] = {
		[TV_AUDIO_SIF]	= "sif",
		[TV_AUDIO_AM]	= "am",
		[TV_AUDIO_FM1]	= "fm1",
		[TV_AUDIO_FM2]	= "fm2",
	};
	cJSON_AddItemToObject(params, "audio", cJSON_CreateString(analogtv_audioName[appControlInfo.tvInfo.audioMode]));

	int res = st_rpcSyncTimeout(elcmd_tvscan, params, RPC_ANALOG_SCAN_TIMEOUT, &type, &result );
	(void)res;
	if(result && result->valuestring != NULL && strcmp (result->valuestring, "ok") == 0) {
		/// TODO

		// elcd dumped services to file. read it
		analogtv_parseConfigFile();
	}
	cJSON_Delete(result);
	cJSON_Delete(params);
#endif

	interface_hideMessageBox();
	pMenu->pActivatedAction(pMenu, pMenu->pArg);
	interface_displayMenu(1);

	offair_fillDVBTMenu();
	if(found_service_count > 0) {
		analogtv_checkServiceNames();
	}
	return 0;
}

void analogtv_stopScan ()
{
	/// TODO
}

int analogtv_start()
{
	/// TODO
	return 0;
}

void analogtv_stop()
{
	/// TODO
	gfx_stopVideoProvider(0, 0, 0);
	appControlInfo.tvInfo.active = 0;
}

void analogtv_init(void)
{
	/// TODO: additional setup

	mysem_create(&analogtv_semaphore);
	analogtv_parseConfigFile();
}

void analogtv_terminate(void)
{
	/// TODO: additional cleanup

	analogtv_stop();

//	analogtv_channelCount = 0;

	mysem_destroy(analogtv_semaphore);
}
//----------------------SET NEXT AUDIO MODE ----  button F3--------------
int32_t analogtv_setNextAudioMode()
{
	printf("%s[%d]\n",__func__, __LINE__);
	char *analogtv_audioName[] = {
		[TV_AUDIO_SIF]	= "sif",
		[TV_AUDIO_AM]	= "am",
		[TV_AUDIO_FM1]	= "fm1",
		[TV_AUDIO_FM2]	= "fm2",
	};
	printf("%s[%d]\n",__func__, __LINE__);
	uint32_t id = appControlInfo.tvInfo.id;
	uint32_t i = 0;
	printf("%s[%d]\n",__func__, __LINE__);
	for (i = 0; i <= TV_AUDIO_FM2; i++) {
		if ( !strcmp(analogtv_channelParam[id].audio, analogtv_audioName[i]) )
			break;
	}
	printf("%s[%d]\n",__func__, __LINE__);
	i++;
	if(i > TV_AUDIO_FM2)
		i = 0;
	
	printf("%s[%d]\n",__func__, __LINE__);
	strncpy(analogtv_channelParam[id].audio, analogtv_audioName[i], sizeof(analogtv_audioName[i]));

	
	printf("Audio mode = %s\n",analogtv_channelParam[id].audio);
	analogtv_activateChannel(interfaceInfo.currentMenu, (void *)id);
	return analogtv_saveConfigFile();
}

int analogtv_playControlProcessCommand(pinterfaceCommandEvent_t cmd, void *pArg)
{
	switch(cmd->command) {
		case interfaceCommand0:
			if (interfaceChannelControl.length)
				return 1;
			if (appControlInfo.offairInfo.previousChannel &&
				appControlInfo.offairInfo.previousChannel != offair_getCurrentChannel())
			{
				offair_setChannel(appControlInfo.offairInfo.previousChannel, SET_NUMBER(screenMain));
			}
			return 0;
		case interfaceCommandGreen:
			if(services_edit_able) {
				analogtv_fillFullServList();
				if(full_service_count > 0) {
					analogtv_menuServicesShow();
				}
			}
			return 0;
		case interfaceCommandMainMenu://DIKS_HOME
		case interfaceCommandYellow:
			analogtv_setNextAudioMode();
			return 0;
		default:;
	}
	return 1;
}

//-----------------------------------------------------------------------
int32_t analogtv_startNextChannel(int32_t direction, void* pArg)
{
	int32_t id = appControlInfo.tvInfo.id;

	id += direction ? -1 : 1;
	if(id < 0) {
		id = analogtv_channelCount - 1;
	} else if(id >= (int32_t)analogtv_channelCount) {
		id = 0;
	}

	analogtv_activateChannel(interfaceInfo.currentMenu, (void *)id);
	return 0;
}
//-----------------------------------------------------------------------

int analogtv_activateChannel(interfaceMenu_t *pMenu, void *pArg)
{
	uint32_t id = (uint32_t)pArg;
	uint32_t freq = analogtv_channelParam[id].frequency;
	char cmd[32];
	int buttons;

	dprintf("%s: in %d\n", __FUNCTION__, id);

	int previousChannel = offair_getCurrentChannel();
	if(appControlInfo.tvInfo.active != 0) {
		//interface_playControlSelect(interfacePlayControlStop);
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
	}

	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	appControlInfo.playbackInfo.streamSource = streamSourceAnalogTV;
	appControlInfo.playbackInfo.channel = id + dvbChannel_getCount();
	appControlInfo.mediaInfo.bHttp = 0;
	appControlInfo.tvInfo.active = 1;
	appControlInfo.tvInfo.id = id;

	buttons  = interfacePlayControlStop|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext;
	buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ?
	           interfacePlayControlAddToPlaylist : interfacePlayControlMode;

	interface_playControlSetInputFocus(inputFocusPlayControl);
	interface_playControlSetup(offair_play_callback, NULL, buttons, analogtv_channelParam[id].customCaption, thumbnail_tvstandard);
	interface_playControlSetDisplayFunction(offair_displayPlayControl);
	interface_playControlSetProcessCommand(analogtv_playControlProcessCommand);
	interface_playControlSetChannelCallbacks(analogtv_startNextChannel, offair_setChannel);
//	interface_playControlSetAudioCallback(offair_audioChanged);
	interface_channelNumberShow(appControlInfo.playbackInfo.channel + 1);

	offair_stopVideo(screenMain, 1);
//	offair_startVideo(screenMain);
// 	offair_fillDVBTMenu();
	offair_updateChannelStatus();
//	saveAppSettings();

	snprintf(cmd, sizeof(cmd), URL_ANALOGTV_MEDIA "%u@%s:%s", freq, analogtv_channelParam[id].sysEncode, analogtv_channelParam[id].audio);

	gfx_startVideoProvider(cmd, 0, 0, NULL);

	if(appControlInfo.tvInfo.active != 0) {
		interface_showMenu(0, 1);
		offair_setPreviousChannel(previousChannel);
	}

	//interface_menuActionShowMenu(pMenu, (void*)&DVBTMenu);

	return 0;
}

void analogtv_initMenu(interfaceMenu_t *pParent)
{
	int offair_icons[4] = { 
		statusbar_f2_find, 
		statusbar_f4_enterurl,
		0,
		0
	};

	analogtv_confInit();

	createListMenu(&AnalogTVChannelMenu, _T("SELECT_CHANNEL"), thumbnail_tvstandard, offair_icons, pParent,
		interfaceListMenuIconThumbnail, NULL, NULL, NULL);

	interface_setCustomKeysCallback(_M &AnalogTVChannelMenu, analogtv_keyCallback);

	analogtv_fillFoundServList();
}

uint32_t analogtv_getChannelCount(void)
{
	analogtv_parseConfigFile();
	return analogtv_channelCount;
}

void analogtv_addChannelsToMenu(interfaceMenu_t *pMenu, int startIndex)
{
	uint32_t i;

	analogtv_parseConfigFile();

	if(analogtv_channelCount == 0) {
		interface_addMenuEntryDisabled(pMenu, _T("NO_CHANNELS"), thumbnail_info);
		return;
	}

	interface_addMenuEntryDisabled(pMenu, "AnalogTV", 0);
	for(i = 0; i < analogtv_channelCount; i++) {
		char channelEntry[32];

		sprintf(channelEntry, "%s. %s", offair_getChannelNumberPrefix(startIndex + i), analogtv_channelParam[i].customCaption);
		interface_addMenuEntry(pMenu, channelEntry, analogtv_activateChannel, (void *)i, thumbnail_tvstandard);
		interface_setMenuEntryLabel(&pMenu->menuEntry[pMenu->menuEntryCount-1], "ANALOG");

		if( (appControlInfo.playbackInfo.streamSource == streamSourceAnalogTV) &&
			(appControlInfo.tvInfo.id == i) )
		{
			interface_setSelectedItem(pMenu, pMenu->menuEntryCount - 1);
		}
	}
}

int menu_entryIsAnalogTv(interfaceMenu_t *pMenu, int index)
{
	return pMenu->menuEntry[pMenu->selectedItem].pAction == analogtv_activateChannel;
}

int analogtv_getServiceDescription(uint32_t index, char *buf, size_t size)
{
	if (index >= analogtv_channelCount) {
		buf[0] = 0;
		return -1;
	}
	sprintf(buf,"\"%s\"\n", analogtv_channelParam[index].customCaption);
	buf += strlen(buf);

	sprintf(buf, "   %s: %u MHz\n", _T("DVB_FREQUENCY"), analogtv_channelParam[index].frequency / 1000000);
	buf += strlen(buf);

	sprintf(buf, "   %s\n", analogtv_channelParam[index].sysEncode);
	buf += strlen(buf);

	sprintf(buf, "   audio:%s", analogtv_channelParam[index].audio);
	buf += strlen(buf);
	return 0;
}

const char * analogtv_getServiceName(uint32_t index)
{
	if (index >= analogtv_channelCount)
		return "";
	return analogtv_channelParam[index].customCaption;
}

int32_t analogtv_hasTuner(void)
{
#ifdef STSDK
	if(st_getBoardId() == eSTB850) {
		return 1;
	}
#endif
	return 0;
}

#endif /* ENABLE_ANALOGTV */
