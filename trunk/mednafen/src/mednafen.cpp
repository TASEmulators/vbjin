/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include        "mednafen.h"

#include        <string.h>
#include	<stdarg.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<unistd.h>
#include	<trio/trio.h>
#include	<list>
#include	<algorithm>

#include	"netplay.h"
#include	"netplay-driver.h"
#include	"general.h"

#include	"state.h"
#include	"movie.h"
#include        "video.h"
#include	"file.h"
#include	"sound/wave.h"
#include	"cdrom/cdromif.h"
#include	"mempatcher.h"
#include	"compress/minilzo.h"
#include	"tests.h"
#include	"video/vblur.h"
#include	"mmrecord.h"
#include	"mmplay.h"
#include	"md5.h"
#include	"clamp.h"
#include	"Fir_Resampler.h"

#include	"string/escape.h"

static const char *CSD_forcemono = gettext_noop("Force monophonic sound output.");
static const char *CSD_enable = gettext_noop("Enable (automatic) usage of this module.");
static const char *CSD_vblur = gettext_noop("Blur each frame with the last frame.");
static const char *CSD_vblur_accum = gettext_noop("Accumulate color data rather than discarding it.");
static const char *CSD_vblur_accum_amount = gettext_noop("Blur amount in accumulation mode, specified in percentage of accumulation buffer to mix with the current frame.");

static bool ValidateSetting(const char *name, const char *value)
{
 if(!strcasecmp(name, "srwcompressor"))
 {
  // If it doesn't match "minilzo", "quicklz", or "blz", error out!
  if(strcasecmp(value, "minilzo") && strcasecmp(value, "quicklz") && strcasecmp(value, "blz"))
   return(FALSE);
 }
 return(TRUE);
}

static MDFNSetting MednafenSettings[] =
{
  { "srwcompressor", gettext_noop("Compressor to use with state rewinding:  \"minilzo\", \"quicklz\", or \"blz\""), MDFNST_STRING, "minilzo", NULL, NULL, ValidateSetting },
  { "srwframes", gettext_noop("Number of frames to keep states for when state rewinding is enabled."), MDFNST_UINT, "600", "10", "99999" },
  { "snapname", gettext_noop("If value is true, use an alternate naming scheme(file base and numeric) for screen snapshots."), MDFNST_BOOL, "0"},
  { "dfmd5", gettext_noop("Include the MD5 hash of the loaded game in the filenames of the data file(save states, SRAM backups) Mednafen creates."), MDFNST_BOOL, "1" },

  { "cdrom.lec_eval", gettext_noop("Enable simple error correction of raw data sector rips by evaluating L-EC and EDC data."), MDFNST_BOOL, "1" },
  // TODO: { "cdrom.ignore_physical_pq", gettext_noop("Ignore P-Q subchannel data when reading physical discs, instead using computed P-Q data."), MDFNST_BOOL, "1" },

  { "path_snap", gettext_noop("Path override for screen snapshots."), MDFNST_STRING, "" },
  { "path_sav", gettext_noop("Path override for save games and nonvolatile memory."), MDFNST_STRING, "" },
  { "path_state", gettext_noop("Path override for save states."), MDFNST_STRING, "" },
  { "path_movie", gettext_noop("Path override for movies."), MDFNST_STRING, "" },
  { "path_cheat", gettext_noop("Path override for cheats."), MDFNST_STRING, "" },
  { "path_palette", gettext_noop("Path override for custom palettes."), MDFNST_STRING, "" },
  { "path_firmware", gettext_noop("Path override for firmware."), MDFNST_STRING, "" },

  { "filesys.snap_samedir", gettext_noop("Write screen snapshots to the same directory the ROM/disk/disc image is in."),
        MDFNST_BOOL, "0" },

  { "filesys.sav_samedir", gettext_noop("Write/Read save games and nonvolatile memory to/from the same directory the ROM/disk/disc image is in."),
	MDFNST_BOOL, "0" },

  { "filesys.state_samedir", gettext_noop("Write/Read save states to/from the same directory the ROM/disk/disc image is in."),
        MDFNST_BOOL, "0" },

  { "filesys.movie_samedir", gettext_noop("Write/Read movies to/from the same directory the ROM/disk/disc image is in."),
        MDFNST_BOOL, "0" },

  { "filesys.disablesavegz", gettext_noop("Disable gzip compression when saving save states and backup memory."), MDFNST_BOOL, "0" },

  { NULL }
};

static char *PortDeviceCache[16];
static void *PortDataCache[16];
static uint32 PortDataLenCache[16];

MDFNGI *MDFNGameInfo = NULL;
static bool CDInUse = 0;

static Fir_Resampler<16> ff_resampler;
static float LastSoundMultiplier;

static bool FFDiscard = FALSE; // TODO:  Setting to discard sound samples instead of increasing pitch

static MDFN_PixelFormat last_pixel_format;


void MDFNI_CloseGame(void)
{
 if(MDFNGameInfo)
 {
  #ifdef NETWORK
  if(MDFNnetplay)
   MDFNI_NetplayStop();
  #endif
  MDFNMOV_Stop();

  if(MDFNGameInfo->GameType != GMT_PLAYER)
   MDFN_FlushGameCheats(NULL);

  MDFNGameInfo->CloseGame();
  if(MDFNGameInfo->name)
  {
   free(MDFNGameInfo->name);
   MDFNGameInfo->name=0;
  }
  MDFNMP_Kill();

  MDFNGameInfo = NULL;
  MDFN_StateEvilEnd();

  if(CDInUse)
  {
   CDIF_Close();
   CDInUse = 0;
  }
 }
 VBlur_Kill();

 #ifdef WANT_DEBUGGER
 MDFNDBG_Kill();
 #endif
}

int MDFNI_NetplayStart(uint32 local_players, uint32 netmerge, const std::string &nickname, const std::string &game_key, const std::string &connect_password)
{
 return(NetplayStart((const char**)PortDeviceCache, PortDataLenCache, local_players, netmerge, nickname, game_key, connect_password));
}


#ifdef WANT_NES_EMU
extern MDFNGI EmulatedNES;
#endif

#ifdef WANT_SNES_EMU
extern MDFNGI EmulatedSNES;
#endif

#ifdef WANT_GBA_EMU
extern MDFNGI EmulatedGBA;
#endif

#ifdef WANT_GB_EMU
extern MDFNGI EmulatedGB;
#endif

#ifdef WANT_LYNX_EMU
extern MDFNGI EmulatedLynx;
#endif

#ifdef WANT_MD_EMU
extern MDFNGI EmulatedMD;
#endif

#ifdef WANT_NGP_EMU
extern MDFNGI EmulatedNGP;
#endif

#ifdef WANT_PCE_EMU
extern MDFNGI EmulatedPCE;
#endif

#ifdef WANT_PCE_FAST_EMU
extern MDFNGI EmulatedPCE_Fast;
#endif

#ifdef WANT_PCFX_EMU
extern MDFNGI EmulatedPCFX;
#endif

#ifdef WANT_VB_EMU
extern MDFNGI EmulatedVB;
#endif

#ifdef WANT_WSWAN_EMU
extern MDFNGI EmulatedWSwan;
#endif

#ifdef WANT_SMS_EMU
extern MDFNGI EmulatedSMS, EmulatedGG;
#endif

std::vector<MDFNGI *> MDFNSystems;
static std::list<MDFNGI *> MDFNSystemsPrio;

bool MDFNSystemsPrio_CompareFunc(MDFNGI *first, MDFNGI *second)
{
 if(first->ModulePriority > second->ModulePriority)
  return(true);

 return(false);
}

static void AddSystem(MDFNGI *system)
{
 MDFNSystems.push_back(system);
}


bool CDIF_DumpCD(const char *fn);

MDFNGI *MDFNI_LoadCD(const char *sysname, const char *devicename)
{
 uint8 LayoutMD5[16];

 MDFNI_CloseGame();

 LastSoundMultiplier = 1;

 int ret = CDIF_Open(devicename);

 if(!ret)
 {
  MDFN_PrintError(_("Error opening CD."));
  return(0);
 }

 // Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
 // its own, or to use it to look up a game in its database.
 {
  CD_TOC toc;
  md5_context layout_md5;

  CDIF_ReadTOC(&toc);

  layout_md5.starts();

  layout_md5.update_u32_as_lsb(toc.first_track);
  layout_md5.update_u32_as_lsb(toc.last_track);
  layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

  for(uint32 track = toc.first_track; track <= toc.last_track; track++)
  {
   layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
   layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
  }

  layout_md5.finish(LayoutMD5);
 }


 //CDIF_DumpCD("/home/sarah/mits/test.dump");

 if(sysname == NULL)
 {
  MDFNGI *default_cdgi = NULL;
  std::list<MDFNGI *>::iterator it;

  for(it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)	//_unsigned int x = 0; x < MDFNSystems.size(); x++)
  {
   char tmpstr[256];
   trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);
   if(!MDFN_GetSettingB(tmpstr))
    continue;

   // Yay, we found a system.
   if((*it)->TestMagicCD)
   {
    // FIXME:  How to select a default for unrecognized CDs?
    if(!default_cdgi)
     default_cdgi = *it;

    if((*it)->TestMagicCD())
    {
     MDFNGameInfo = *it;
     break;
    }
   }
  }

  if(!MDFNGameInfo) 
  {
   if(!default_cdgi)
   {
    MDFN_PrintError(_("No supported systems support CD emulation!"));
    return(0);
   }
   else
    MDFNGameInfo = default_cdgi;
  }
 }
 else
 {
  for(unsigned int x = 0; x < MDFNSystems.size(); x++)
  {
   if(!strcasecmp(MDFNSystems[x]->shortname, sysname))
   {
    if(!MDFNSystems[x]->LoadCD)
    {
     MDFN_PrintError(_("Specified system \"%s\" doesn't support CDs!"), sysname);
     return(0);
    }
    MDFNGameInfo = MDFNSystems[x];
    break;
   }
  }
  if(!MDFNGameInfo)
  {
   MDFN_PrintError(_("Unrecognized system \"%s\"!"), sysname);
   return(0);
  }
 }

 // TODO: include module name in hash
 memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

 if(!(MDFNGameInfo->LoadCD()))
 {
  CDIF_Close();
  MDFNGameInfo = NULL;
  return(0);
 }
 CDInUse = 1;

 #ifdef WANT_DEBUGGER
 MDFNDBG_PostGameLoad(); 
 #endif

 MDFNSS_CheckStates();
 MDFNMOV_CheckMovies();

 MDFN_ResetMessages();   // Save state, status messages, etc.

 VBlur_Init();

 MDFN_StateEvilBegin();
 return(MDFNGameInfo);
}

// Return FALSE on fatal error(IPS file found but couldn't be applied),
// or TRUE on success(IPS patching succeeded, or IPS file not found).
static bool LoadIPS(MDFNFILE &GameFile, const char *path)
{
 MDFNFILE IPSFile;

 if(!IPSFile.Open(path, NULL, "patch file", TRUE))
 {
  if(IPSFile.GetErrorCode() == MDFNFILE_EC_NOTFOUND)
   return(1);
  else
  {
   return(0);
  }
 }

 MDFN_printf(_("Applying IPS file \"%s\"...\n"), path);

 if(!GameFile.ApplyIPS(&IPSFile))
 {
  IPSFile.Close();
  return(0);
 }
 IPSFile.Close();

 return(1);
}

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
        MDFNFILE GameFile;
	struct stat stat_buf;
	std::vector<FileExtensionSpecStruct> valid_iae;

	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc")))
	{
	 return(MDFNI_LoadCD(force_module, name));
	}
	
	if(!stat(name, &stat_buf) && !S_ISREG(stat_buf.st_mode))
	{
	 return(MDFNI_LoadCD(force_module, name));
	}

	MDFNI_CloseGame();

	LastSoundMultiplier = 1;

	MDFNGameInfo = NULL;

	MDFN_printf(_("Loading %s...\n"),name);

	MDFN_indent(1);

        GetFileBase(name);

	if(MMPlay_Load(name) > 0)
	{
	 MDFNGameInfo = &MMPlayGI;
	 goto SkipNormalLoad;
	}

	// Construct a NULL-delimited list of known file extensions for MDFN_fopen()
	for(unsigned int i = 0; i < MDFNSystems.size(); i++)
	{
	 const FileExtensionSpecStruct *curexts = MDFNSystems[i]->FileExtensions;

	 // If we're forcing a module, only look for extensions corresponding to that module
	 if(force_module && strcmp(MDFNSystems[i]->shortname, force_module))
	  continue;

	 if(curexts)	
 	  while(curexts->extension && curexts->description)
	  {
	   valid_iae.push_back(*curexts);
           curexts++;
 	  }
	}
	{
	 FileExtensionSpecStruct tmpext = { NULL, NULL };
	 valid_iae.push_back(tmpext);
	}

	if(!GameFile.Open(name, &valid_iae[0], _("game")))
        {
	 MDFNGameInfo = NULL;
	 return 0;
	}

	if(!LoadIPS(GameFile, MDFN_MakeFName(MDFNMKF_IPS, 0, 0).c_str()))
	{
	 MDFNGameInfo = NULL;
         GameFile.Close();
         return(0);
	}

	MDFNGameInfo = NULL;

	for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)  //_unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	 char tmpstr[256];
	 trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);

	 if(force_module)
	 {
          if(!(*it)->Load)
           continue;

          if(!strcmp(force_module, (*it)->shortname))
          {
           MDFNGameInfo = *it;
           break;
          }
	 }
	 else
	 {
	  // Is module enabled?
	  if(!MDFN_GetSettingB(tmpstr))
	   continue; 

	  if(!(*it)->Load || !(*it)->TestMagic)
	   continue;

	  if((*it)->TestMagic(name, &GameFile))
	  {
	   MDFNGameInfo = *it;
	   break;
	  }
	 }
	}

        if(!MDFNGameInfo)
        {
	 GameFile.Close();

         MDFN_PrintError(_("Unrecognized file format.  Sorry."));
         MDFN_indent(-1);
         MDFNGameInfo = NULL;
         return 0;
        }

	MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);
	MDFN_indent(1);

	assert(MDFNGameInfo->soundchan != 0);

        MDFNGameInfo->soundrate = 0;
        MDFNGameInfo->name = NULL;
        MDFNGameInfo->rotated = 0;

        if(MDFNGameInfo->Load(name, &GameFile) <= 0)
	{
         GameFile.Close();
         MDFN_indent(-2);
         MDFNGameInfo = NULL;
         return(0);
        }

        if(MDFNGameInfo->GameType != GMT_PLAYER)
	{
	 MDFN_LoadGameCheats(NULL);
	 MDFNMP_InstallReadPatches();
	}

	SkipNormalLoad: ;

	#ifdef WANT_DEBUGGER
	MDFNDBG_PostGameLoad();
	#endif

	MDFNSS_CheckStates();
	MDFNMOV_CheckMovies();

	MDFN_ResetMessages();	// Save state, status messages, etc.

	MDFN_indent(-2);

	if(!MDFNGameInfo->name)
        {
         unsigned int x;
         char *tmp;

         MDFNGameInfo->name = (UTF8 *)strdup(GetFNComponent(name));

         for(x=0;x<strlen((char *)MDFNGameInfo->name);x++)
         {
          if(MDFNGameInfo->name[x] == '_')
           MDFNGameInfo->name[x] = ' ';
         }
         if((tmp = strrchr((char *)MDFNGameInfo->name, '.')))
          *tmp = 0;
        }

	VBlur_Init();

        MDFN_StateEvilBegin();

        return(MDFNGameInfo);
}

static void BuildDynamicSetting(MDFNSetting *setting, const char *system_name, const char *name, const char *description, MDFNSettingType type,
        const char *default_value, const char *minimum = NULL, const char *maximum = NULL,
        bool (*validate_func)(const char *name, const char *value) = NULL, void (*ChangeNotification)(const char *name) = NULL)
{
 char setting_name[256];

 memset(setting, 0, sizeof(MDFNSetting));

 trio_snprintf(setting_name, 256, "%s.%s", system_name, name);

 setting->name = strdup(setting_name);
 setting->description = description;
 setting->type = type;
 setting->default_value = default_value;
 setting->minimum = minimum;
 setting->maximum = maximum;
 setting->validate_func = validate_func;
 setting->ChangeNotification = ChangeNotification;
}

std::vector<std::string> string_to_vecstrlist(const std::string &str_str)
{
 std::vector<std::string> ret;
 const char *str = str_str.c_str();

 bool in_quote = FALSE;
 const char *quote_begin = NULL;
 char last_char = 0;

 while(*str || in_quote)
 {
  char c;

  if(*str)
   c = *str;
  else		// If the string has ended and we're still in a quote, get out of it!
  {
   c = '"';
   last_char = 0;
  }

  if(last_char != '\\')
  {
   if(c == '"')
   {
    if(in_quote)
    {
     int64 str_length = str - quote_begin;
     char tmp_str[str_length];

     memcpy(tmp_str, quote_begin, str_length);
  
     ret.push_back(std::string(tmp_str));

     quote_begin = NULL;
     in_quote = FALSE;
    }
    else
    {
     in_quote = TRUE;
     quote_begin = str + 1;
    }
   }
  }

  last_char = c;

  if(*str)
   str++;
 }


 return(ret);
}

std::string vecstrlist_to_string(const std::vector<std::string> &vslist)
{
 std::string ret;

 for(uint32 i = 0; i < vslist.size(); i++)
 {
  char *tmp_str = escape_string(vslist[i].c_str());

  ret += "\"";
 
  ret += std::string(tmp_str);
 
  ret += "\" ";

  free(tmp_str);
 }
 return(ret);
}


bool MDFNI_InitializeModules(const std::vector<MDFNGI *> &ExternalSystems)
{
 static MDFNGI *InternalSystems[] =
 {
  #ifdef WANT_NES_EMU
  &EmulatedNES,
  #endif

  #ifdef WANT_SNES_EMU
  &EmulatedSNES,
  #endif

  #ifdef WANT_GB_EMU
  &EmulatedGB,
  #endif

  #ifdef WANT_GBA_EMU
  &EmulatedGBA,
  #endif

  #ifdef WANT_PCE_EMU
  &EmulatedPCE,
  #endif

  #ifdef WANT_PCE_FAST_EMU
  &EmulatedPCE_Fast,
  #endif

  #ifdef WANT_LYNX_EMU
  &EmulatedLynx,
  #endif

  #ifdef WANT_MD_EMU
  &EmulatedMD,
  #endif

  #ifdef WANT_PCFX_EMU
  &EmulatedPCFX,
  #endif

  #ifdef WANT_NGP_EMU
  &EmulatedNGP,
  #endif

  #ifdef WANT_VB_EMU
  &EmulatedVB,
  #endif

  #ifdef WANT_WSWAN_EMU
  &EmulatedWSwan,
  #endif

  #ifdef WANT_SMS_EMU
  &EmulatedSMS,
  &EmulatedGG,
  #endif

  &MMPlayGI // Needed for the driver code to generate input device structures...
 };
 std::string i_modules_string, e_modules_string;

 MDFNI_printf(_("Starting Mednafen %s\n"), MEDNAFEN_VERSION);
 MDFN_indent(1);

 for(unsigned int i = 0; i < sizeof(InternalSystems) / sizeof(MDFNGI *); i++)
 {
  AddSystem(InternalSystems[i]);
  if(i)
   i_modules_string += " ";
  i_modules_string += std::string(InternalSystems[i]->shortname);
 }

 for(unsigned int i = 0; i < ExternalSystems.size(); i++)
 {
  AddSystem(ExternalSystems[i]);
  if(i)
   i_modules_string += " ";
  e_modules_string += std::string(ExternalSystems[i]->shortname);
 }

 MDFNI_printf(_("Internal emulation modules: %s\n"), i_modules_string.c_str());
 MDFNI_printf(_("External emulation modules: %s\n"), e_modules_string.c_str());


 for(unsigned int i = 0; i < MDFNSystems.size(); i++)
  MDFNSystemsPrio.push_back(MDFNSystems[i]);

 MDFNSystemsPrio.sort(MDFNSystemsPrio_CompareFunc);

 #if 0
 std::string a_modules;

 std::list<MDFNGI *>:iterator it;

 for(it = MDFNSystemsPrio.
 f
 #endif

 return(1);
}

int MDFNI_Initialize(const char *basedir, const std::vector<MDFNSetting> &DriverSettings)
{
	// FIXME static
	static std::vector<MDFNSetting> dynamic_settings;

	if(!MDFN_RunMathTests())
	{
	 return(0);
	}

	memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

	memset(PortDataCache, 0, sizeof(PortDataCache));
	memset(PortDataLenCache, 0, sizeof(PortDataLenCache));
	memset(PortDeviceCache, 0, sizeof(PortDeviceCache));

	lzo_init();

	MDFNI_SetBaseDirectory(basedir);

        memset(&FSettings,0,sizeof(FSettings));

	MDFN_InitFontData();

	// Generate dynamic settings
	for(unsigned int i = 0; i < MDFNSystems.size(); i++)
	{
	 MDFNSetting setting;
	 const char *sysname;
	
	 sysname = (const char *)MDFNSystems[i]->shortname;
 
	 if(!MDFNSystems[i]->soundchan)
	  printf("0 sound channels for %s????\n", sysname);

	 if(MDFNSystems[i]->soundchan == 2)
	 {
	  BuildDynamicSetting(&setting, sysname, "forcemono", CSD_forcemono, MDFNST_BOOL, "0");
	  dynamic_settings.push_back(setting);
	 }

	 BuildDynamicSetting(&setting, sysname, "enable", CSD_enable, MDFNST_BOOL, "1");
	 dynamic_settings.push_back(setting);

	 BuildDynamicSetting(&setting, sysname, "vblur", CSD_vblur, MDFNST_BOOL, "0");
         dynamic_settings.push_back(setting);

         BuildDynamicSetting(&setting, sysname, "vblur.accum", CSD_vblur_accum, MDFNST_BOOL, "0");
         dynamic_settings.push_back(setting);

         BuildDynamicSetting(&setting, sysname, "vblur.accum.amount", CSD_vblur_accum_amount, MDFNST_FLOAT, "50", "0", "100");
	 dynamic_settings.push_back(setting);
	}

	// First merge all settable settings, then load the settings from the SETTINGS FILE OF DOOOOM
	MDFN_MergeSettings(MednafenSettings);
        MDFN_MergeSettings(dynamic_settings);
	MDFN_MergeSettings(MDFNMP_Settings);

	if(DriverSettings.size())
 	 MDFN_MergeSettings(DriverSettings);

	for(unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	 if(MDFNSystems[x]->Settings)
	  MDFN_MergeSettings(MDFNSystems[x]->Settings);
	}

        if(!MFDN_LoadSettings(basedir))
	 return(0);

	#ifdef WANT_DEBUGGER
	MDFNDBG_Init();
	#endif

        return(1);
}

void MDFNI_Kill(void)
{
 MDFN_SaveSettings();

 for(unsigned int x = 0; x < sizeof(PortDeviceCache) / sizeof(char *); x++)
 {
  if(PortDeviceCache[x])
  {
   free(PortDeviceCache[x]);
   PortDeviceCache[x] = NULL;
  }
 }
}

void MDFNI_Emulate(EmulateSpecStruct *espec)
{
 double multiplier_save = 1;
 double volume_save = 1;


 assert((bool)(espec->SoundBuf != NULL) == (bool)FSettings.SndRate);

 espec->SoundBufSize = 0;

 espec->VideoFormatChanged = FALSE;

 if(memcmp(&last_pixel_format, &espec->surface->format, sizeof(MDFN_PixelFormat)))
 {
  espec->VideoFormatChanged = TRUE;

  last_pixel_format = espec->surface->format;
 }

 // We want to record movies without any dropped video frames and without fast-forwarding sound distortion and without custom volume.
 // The same goes for WAV recording(sans the dropped video frames bit :b).
 if(MMRecord_Active() || MDFN_WaveRecordActive() || FFDiscard)
 {
  multiplier_save = espec->soundmultiplier;
  espec->soundmultiplier = 1;

  if(!FFDiscard)
  {
   volume_save = espec->SoundVolume;
   espec->SoundVolume = 1;
  }
 }

 #ifdef NETWORK
 if(MDFNnetplay)
 {
  NetplayUpdate((const char**)PortDeviceCache, PortDataCache, PortDataLenCache, MDFNGameInfo->InputInfo->InputPorts);
 }
 #endif

 for(int x = 0; x < 16; x++)
  if(PortDataCache[x])
   MDFNMOV_AddJoy(PortDataCache[x], PortDataLenCache[x]);

 if(MMRecord_Active())
  espec->skip = 0;

 if(VBlur_IsOn())
  espec->skip = 0;

 if(espec->NeedRewind)
 {
  if(MDFNMOV_IsPlaying())
  {
   espec->NeedRewind = 0;
   MDFN_DispMessage(_("Can't rewind during movie playback(yet!)."));
  }
  else if(MDFNnetplay)
  {
   espec->NeedRewind = 0;
   MDFN_DispMessage(_("Silly-billy, can't rewind during netplay."));
  }
  else if(MDFNGameInfo->GameType == GMT_PLAYER)
  {
   espec->NeedRewind = 0;
   MDFN_DispMessage(_("Music player rewinding is unsupported."));
  }
 }

 espec->NeedSoundReverse = MDFN_StateEvil(espec->NeedRewind);

 MDFNGameInfo->Emulate(espec);

 VBlur_Run(espec);

 if(MMRecord_Active())
  MMRecord_WriteFrame(espec);

 if(espec->SoundBuf && espec->SoundBufSize)
 {
  if(espec->NeedSoundReverse)
  {
   int16 *yaybuf = espec->SoundBuf;
   int32 slen = espec->SoundBufSize;

   if(MDFNGameInfo->soundchan == 1)
   {
    for(int x = 0; x < (slen / 2); x++)    
    {
     int16 cha = yaybuf[slen - x - 1];
     yaybuf[slen - x - 1] = yaybuf[x];
     yaybuf[x] = cha;
    }
   }
   else if(MDFNGameInfo->soundchan == 2)
   {
    for(int x = 0; x < (slen * 2) / 2; x++)
    {
     int16 cha = yaybuf[slen * 2 - (x&~1) - ((x&1) ^ 1) - 1];
     yaybuf[slen * 2 - (x&~1) - ((x&1) ^ 1) - 1] = yaybuf[x];
     yaybuf[x] = cha;
    }
   }
  }
  MDFN_WriteWaveData(espec->SoundBuf, espec->SoundBufSize); /* This function will just return if sound recording is off. */

  if(volume_save != 1)
  {
   espec->SoundVolume = volume_save;
   volume_save = 1;
  }

  if(multiplier_save != 1)
  {
   if(FFDiscard)
   {
    if(espec->SoundBufSize >= multiplier_save)
     espec->SoundBufSize /= multiplier_save;
   }
   else
   {
    espec->soundmultiplier = multiplier_save;
    multiplier_save = 1;
   }
  }

  if(espec->soundmultiplier != LastSoundMultiplier)
  {
   //MDFNGameInfo->SetSoundMultiplier(espec->soundmultiplier);
   //MDFNGameInfo->SetSoundRate(FSettings.SndRate / espec->soundmultiplier);
   ff_resampler.time_ratio((double)espec->soundmultiplier, 0.9965);
   LastSoundMultiplier = espec->soundmultiplier;
  }

  if(espec->soundmultiplier != 1)
  {
   if(MDFNGameInfo->soundchan == 2)
   {
    assert(ff_resampler.max_write() >= espec->SoundBufSize * 2);

    for(int i = 0; i < espec->SoundBufSize * 2; i++)
     ff_resampler.buffer()[i] = espec->SoundBuf[i];
   }
   else
   {
    assert(ff_resampler.max_write() >= espec->SoundBufSize * 2);

    for(int i = 0; i < espec->SoundBufSize; i++)
    {
     ff_resampler.buffer()[i * 2] = espec->SoundBuf[i];
     ff_resampler.buffer()[i * 2 + 1] = 0;
    }
   }  
   ff_resampler.write(espec->SoundBufSize * 2);

   int avail = ff_resampler.avail();
   int real_read = std::min((int)(espec->SoundBufMaxSize * MDFNGameInfo->soundchan), avail);

   if(MDFNGameInfo->soundchan == 2)
    espec->SoundBufSize = ff_resampler.read(espec->SoundBuf, real_read ) >> 1;
   else
    espec->SoundBufSize = ff_resampler.read_mono_hack(espec->SoundBuf, real_read );

   avail -= real_read;

   if(avail > 0)
   {
    printf("ff_resampler.avail() > espec->SoundBufMaxSize * MDFNGameInfo->soundchan - %d\n", avail);
    ff_resampler.clear();
   }
  }

  if(espec->SoundVolume != 1)
  {
   if(espec->SoundVolume < 1)
   {
    int volume = (int)(16384 * espec->SoundVolume);

    for(int i = 0; i < espec->SoundBufSize * MDFNGameInfo->soundchan; i++)
     espec->SoundBuf[i] = (espec->SoundBuf[i] * volume) >> 14;
   }
   else
   {
    int volume = (int)(256 * espec->SoundVolume);

    for(int i = 0; i < espec->SoundBufSize * MDFNGameInfo->soundchan; i++)
    {
     int temp = ((espec->SoundBuf[i] * volume) >> 8) + 32768;

     temp = clamp_to_u16(temp);

     espec->SoundBuf[i] = temp - 32768;
    }
   }
  }

  // TODO: Optimize this.
  if(MDFNGameInfo->soundchan == 2 && MDFN_GetSettingB(std::string(std::string(MDFNGameInfo->shortname) + ".forcemono").c_str()))
  {
   for(int i = 0; i < espec->SoundBufSize * MDFNGameInfo->soundchan; i += 2)
   {
    // We should use division instead of arithmetic right shift for correctness(rounding towards 0 instead of negative infinitininintinity), but I like speed.
    int32 mixed = (espec->SoundBuf[i + 0] + espec->SoundBuf[i + 1]) >> 1;

    espec->SoundBuf[i + 0] =
    espec->SoundBuf[i + 1] = mixed;
   }
  }

 } // end to:  if(espec->SoundBuf && espec->SoundBufSize)
}

// This function should only be called for state rewinding.
// FIXME:  Add a macro for SFORMAT structure access instead of direct access
int MDFN_RawInputStateAction(StateMem *sm, int load, int data_only)
{
 static const char *stringies[16] = { "RI00", "RI01", "RI02", "RI03", "RI04", "RI05", "RI06", "RI07", "RI08", "RI09", "RI0a", "RI0b", "RI0c", "RI0d", "RI0e", "RI0f" };
 SFORMAT StateRegs[17];
 int x;

 for(x = 0; x < 16; x++)
 {
  StateRegs[x].name = stringies[x];
  StateRegs[x].flags = 0;

  if(PortDataCache[x])
  {
   StateRegs[x].v = PortDataCache[x];
   StateRegs[x].size = PortDataLenCache[x];
  }
  else
  {
   StateRegs[x].v = NULL;
   StateRegs[x].size = 0;
  }
 }

 StateRegs[x].v = NULL;
 StateRegs[x].size = 0;
 StateRegs[x].name = NULL;

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "rinp");

 return(ret);
}


MDFNS FSettings;

static int curindent = 0;

void MDFN_indent(int indent)
{
 curindent += indent;
}

static uint8 lastchar = 0;
void MDFN_printf(const char *format, ...)
{
 char *format_temp;
 char *temp;
 unsigned int x, newlen;

 va_list ap;
 va_start(ap,format);


 // First, determine how large our format_temp buffer needs to be.
 uint8 lastchar_backup = lastchar; // Save lastchar!
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    newlen++;
  }
  newlen++;
  lastchar = format[x];
 }

 format_temp = (char *)malloc(newlen + 1); // Length + NULL character, duh
 
 // Now, construct our format_temp string
 lastchar = lastchar_backup; // Restore lastchar
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    format_temp[newlen++] = ' ';
  }
  format_temp[newlen++] = format[x];
  lastchar = format[x];
 }

 format_temp[newlen] = 0;

 temp = trio_vaprintf(format_temp, ap);
 free(format_temp);

 MDFND_Message(temp);
 free(temp);

 va_end(ap);
}

void MDFN_PrintError(const char *format, ...)
{
 char *temp;

 va_list ap;

 va_start(ap, format);

 temp = trio_vaprintf(format, ap);
 MDFND_PrintError(temp);
 free(temp);

 va_end(ap);
}

MDFNException::MDFNException()
{


}

MDFNException::~MDFNException()
{


}

void MDFNException::AddPre(const char *format, ...)
{
 char oldmsg[sizeof(TheMessage)];

 strcpy(oldmsg, TheMessage);

 va_list ap;
 va_start(ap, format);
 trio_vsnprintf(oldmsg, sizeof(TheMessage), format, ap);
 va_end(ap);

 int freelen = sizeof(TheMessage) - strlen(TheMessage);
 strncpy(TheMessage + strlen(TheMessage), oldmsg, freelen);
}

void MDFNException::AddPost(const char *format, ...)
{
 int freelen = sizeof(TheMessage) - strlen(TheMessage);

 if(freelen <= 0)
 {
  puts("ACKACKACK Exception erorrorololoz");
  return;
 }

 va_list ap;

 va_start(ap, format);
 trio_vsnprintf(TheMessage + strlen(TheMessage), freelen, format, ap);
 va_end(ap);
}

void MDFN_DoSimpleCommand(int cmd)
{
 MDFNGameInfo->DoSimpleCommand(cmd);
}

void MDFN_QSimpleCommand(int cmd)
{
 #ifdef NETWORK
 if(MDFNnetplay)
  MDFNNET_SendCommand(cmd, 0);
 else
 #endif
 {
  if(!MDFNMOV_IsPlaying())
  {
   MDFN_DoSimpleCommand(cmd);
   MDFNMOV_AddCommand(cmd);
  }
 }
}

void MDFNI_Power(void)
{
 if(MDFNGameInfo)
  MDFN_QSimpleCommand(MDFNNPCMD_POWER);
}

void MDFNI_Reset(void)
{
 if(MDFNGameInfo)
  MDFN_QSimpleCommand(MDFNNPCMD_RESET);
}

bool MDFNI_SetSoundRate(uint32 rate)
{
 FSettings.SndRate = rate;

 if(!MDFNGameInfo)
  return(FALSE);

 ff_resampler.buffer_size((rate / 2) * 2);

 return(MDFNGameInfo->SetSoundRate(rate));
}

void MDFNI_ToggleLayer(int which)
{
 if(MDFNGameInfo)
 {
  const char *goodies = MDFNGameInfo->LayerNames;
  int x = 0;
  while(x != which)
  {
   while(*goodies)
    goodies++;
   goodies++;
   if(!*goodies) return; // ack, this layer doesn't exist.
   x++;
  }
  if(MDFNGameInfo->ToggleLayer(which))
   MDFN_DispMessage(_("%s enabled."), _(goodies));
  else
   MDFN_DispMessage(_("%s disabled."), _(goodies));
 }
}

void MDFNI_SetInput(int port, const char *type, void *ptr, uint32 ptr_len_thingy)
{
 if(MDFNGameInfo)
 {
  assert(port < 16);

  PortDataCache[port] = ptr;
  PortDataLenCache[port] = ptr_len_thingy;

  if(PortDeviceCache[port])
  {
   free(PortDeviceCache[port]);
   PortDeviceCache[port] = NULL;
  }

  PortDeviceCache[port] = strdup(type);

  MDFNGameInfo->SetInput(port, type, ptr);
 }
}

int MDFNI_DiskInsert(int oride)
{
 if(MDFNGameInfo && MDFNGameInfo->DiskInsert)
  return(MDFNGameInfo->DiskInsert(oride));

 return(0);
}

int MDFNI_DiskEject(void)
{
 if(MDFNGameInfo && MDFNGameInfo->DiskEject)
  return(MDFNGameInfo->DiskEject());

 return(0);
}

int MDFNI_DiskSelect(void)
{
 if(MDFNGameInfo && MDFNGameInfo->DiskSelect)
  return(MDFNGameInfo->DiskSelect());

 return(0);
}
