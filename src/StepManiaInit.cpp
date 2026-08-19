/* Functions which only run once, or on game startup. */

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "Bookkeeper.h"
#include "CharacterManager.h"
#include "CodeDetector.h"
#include "CommandLineActions.h"
#include "CommonMetrics.h"
#include "CryptManager.h"
#include "FontManager.h"
#include "Game.h"
#include "GameLoop.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "ImageCache.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "InputQueue.h"
#include "LightsManager.h"
#include "LocalizedString.h"
#include "LuaDebugManager.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include "NoteSkinManager.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProductInfo.h"
#include "ProfileManager.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageFileManager.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSurface.h"
#include "RageSurface_Load.h"
#include "RageTextureManager.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "ScreenManager.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "SpecialFiles.h"
#include "StatsManager.h"
#include "StepMania.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/Dialog/Dialog.h"
#include "arch/LoadingWindow/LoadingWindow.h"
#include "global.h"
#include "ver.h"

#if !defined(SUPPORT_OPENGL) && !defined(SUPPORT_D3D)
#define SUPPORT_OPENGL
#endif

#if defined(_WIN32)
#include "RageDisplay_D3D.h"
#include "archutils/Win32/VideoDriverInfo.h"
#endif

#if defined(SUPPORT_OPENGL)
#include "RageDisplay_OGL.h"
#endif

#if defined(SUPPORT_GLES2)
#include "RageDisplay_GLES2.h"
#endif

#include "RageDisplay_Null.h"

static Preference<bool> g_bAllowMultipleInstances(
    "AllowMultipleInstances", false);
static LoadingWindow* g_pLoadingWindow = nullptr;

static LocalizedString COLOR("StepMania", "color");
static LocalizedString TEXTURE("StepMania", "texture");
static LocalizedString WINDOWED("StepMania", "Windowed");
static LocalizedString FULLSCREEN("StepMania", "Fullscreen");
static LocalizedString VSYNC("StepMania", "Vsync");
static LocalizedString NO_VSYNC("StepMania", "NoVsync");
static LocalizedString SMOOTH_LINES("StepMania", "SmoothLines");
static LocalizedString NO_SMOOTH_LINES("StepMania", "NoSmoothLines");

static LocalizedString ERROR_INITIALIZING_CARD(
    "StepMania", "There was an error while initializing your video card.");
static LocalizedString ERROR_DONT_FILE_BUG(
    "StepMania",
    "Please do not file this error as a bug!  Use the web page below to "
    "troubleshoot this problem.");
static LocalizedString ERROR_VIDEO_DRIVER("StepMania", "Video Driver: %s");
static LocalizedString ERROR_NO_VIDEO_RENDERERS(
    "StepMania", "No video renderers attempted.");
static LocalizedString ERROR_INITIALIZING("StepMania", "Initializing %s...");
static LocalizedString ERROR_UNKNOWN_VIDEO_RENDERER(
    "StepMania", "Unknown video renderer value: %s");
static LocalizedString COULDNT_OPEN_LOADING_WINDOW(
    "LoadingWindow", "Couldn't open any loading windows.");

static std::string GetActualGraphicOptionsString();
static void StoreActualGraphicOptions();
static void update_centering();
static void StartDisplay();
static bool CheckVideoDefaultSettings();
static RageDisplay* CreateDisplay();
static void ShutdownGame();
static void HandleException(const std::string& sError);
static void SwitchToLastPlayedGame();
static void MountTreeOfZips(const std::string& dir);
static void MountFolders(
    const std::string& type, const std::string& realPathList,
    const std::string& mountPoint);
static void WriteLogHeader();
static void ApplyLogPreferences();

class Bootstrap {
 public:
  int Run(int argc, char* argv[]);

 private:
};

void StepMania::GetPreferredVideoModeParams(VideoModeParams& paramsOut) {
  int iWidth = PREFSMAN->m_iDisplayWidth;
  if (PREFSMAN->m_bWindowed) {
    iWidth =
        std::ceil(PREFSMAN->m_iDisplayHeight * PREFSMAN->m_fDisplayAspectRatio);
    iWidth -= iWidth % 2;
  }

  paramsOut = VideoModeParams(
      PREFSMAN->m_bWindowed || PREFSMAN->m_bFullscreenIsBorderlessWindow,
      PREFSMAN->m_sDisplayId, iWidth, PREFSMAN->m_iDisplayHeight,
      PREFSMAN->m_iDisplayColorDepth, PREFSMAN->m_iRefreshRate,
      PREFSMAN->m_bVsync, PREFSMAN->m_bInterlaced, PREFSMAN->m_bSmoothLines,
      PREFSMAN->m_bTrilinearFiltering, PREFSMAN->m_bAnisotropicFiltering,
      !PREFSMAN->m_bWindowed && PREFSMAN->m_bFullscreenIsBorderlessWindow,
      CommonMetrics::WINDOW_TITLE, THEME->GetPathG("Common", "window icon"),
      PREFSMAN->m_bPAL, PREFSMAN->m_fDisplayAspectRatio);
}

static std::string GetActualGraphicOptionsString() {
  const VideoModeParams& params = DISPLAY->GetActualVideoModeParams();
  std::string sFormat = "%s %s %dx%d %d " + COLOR.GetValue() + " %d " +
                        TEXTURE.GetValue() + " %dHz %s %s";
  std::string sLog = ssprintf(
      sFormat.c_str(), DISPLAY->GetApiDescription().c_str(),
      (params.windowed ? WINDOWED : FULLSCREEN).GetValue().c_str(),
      (int)params.width, (int)params.height, (int)params.bpp,
      (int)PREFSMAN->m_iTextureColorDepth, (int)params.rate,
      (params.vsync ? VSYNC : NO_VSYNC).GetValue().c_str(),
      (PREFSMAN->m_bSmoothLines ? SMOOTH_LINES : NO_SMOOTH_LINES)
          .GetValue()
          .c_str());
  return sLog;
}

static void StoreActualGraphicOptions() {
  const VideoModeParams& params = DISPLAY->GetActualVideoModeParams();
  PREFSMAN->m_bWindowed.Set(
      params.windowed && !params.bWindowIsFullscreenBorderless);
  if (!params.windowed && !params.bWindowIsFullscreenBorderless) {
    PREFSMAN->m_bFullscreenIsBorderlessWindow.Set(false);
  }

  if (!PREFSMAN->m_bWindowed) {
    PREFSMAN->m_iDisplayWidth.Set(params.width);
    PREFSMAN->m_iDisplayHeight.Set(params.height);
  }
  PREFSMAN->m_iDisplayColorDepth.Set(params.bpp);
  if (PREFSMAN->m_iRefreshRate != REFRESH_DEFAULT) {
    PREFSMAN->m_iRefreshRate.Set(params.rate);
  }
  PREFSMAN->m_bVsync.Set(params.vsync);

  Dialog::SetWindowed(params.windowed);
}

bool StepMania::GetHighResolutionTextures() {
  switch (PREFSMAN->m_HighResolutionTextures) {
    default:
    case HighResolutionTextures_Auto: {
      int height = PREFSMAN->m_iDisplayHeight;
      return height > THEME->GetMetricI("Common", "ScreenHeight");
    }
    case HighResolutionTextures_ForceOn:
      return true;
    case HighResolutionTextures_ForceOff:
      return false;
  }
}

static void update_centering() {
  DISPLAY->ChangeCentering(
      PREFSMAN->m_iCenterImageTranslateX, PREFSMAN->m_iCenterImageTranslateY,
      PREFSMAN->m_fCenterImageAddWidth, PREFSMAN->m_fCenterImageAddHeight);
}

static void StartDisplay() {
  if (DISPLAY != nullptr) {
    return;
  }

  DISPLAY = CreateDisplay();

  update_centering();

  TEXTUREMAN = new RageTextureManager;
  TEXTUREMAN->SetPrefs(RageTextureManagerPrefs(
      PREFSMAN->m_iTextureColorDepth, PREFSMAN->m_iMovieColorDepth,
      PREFSMAN->m_bDelayedTextureDelete, PREFSMAN->m_iMaxTextureResolution,
      StepMania::GetHighResolutionTextures(), PREFSMAN->m_bForceMipMaps));

  MODELMAN = new ModelManager;
  MODELMAN->SetPrefs(ModelManagerPrefs(PREFSMAN->m_bDelayedModelDelete));
}

void StepMania::ApplyGraphicOptions() {
  bool bNeedReload = false;

  VideoModeParams params;
  GetPreferredVideoModeParams(params);
  std::string sError = DISPLAY->SetVideoMode(params, bNeedReload);
  if (sError != "") {
    RageException::Throw("%s", sError.c_str());
  }

  update_centering();

  bNeedReload |= TEXTUREMAN->SetPrefs(RageTextureManagerPrefs(
      PREFSMAN->m_iTextureColorDepth, PREFSMAN->m_iMovieColorDepth,
      PREFSMAN->m_bDelayedTextureDelete, PREFSMAN->m_iMaxTextureResolution,
      StepMania::GetHighResolutionTextures(), PREFSMAN->m_bForceMipMaps));

  bNeedReload |=
      MODELMAN->SetPrefs(ModelManagerPrefs(PREFSMAN->m_bDelayedModelDelete));

  if (bNeedReload) {
    TEXTUREMAN->ReloadAll();
  }

  StoreActualGraphicOptions();
  if (SCREENMAN) {
    SCREENMAN->SystemMessage(GetActualGraphicOptionsString());
  }

  INPUTMAN->WindowReset();
}

void StepMania::ResetPreferences() {
  PREFSMAN->ResetToFactoryDefaults();
  SOUNDMAN->SetMixVolume();
  CheckVideoDefaultSettings();
  ApplyGraphicOptions();
}

void StepMania::InitializeCurrentGame(const Game* g) {
  ASSERT(g != nullptr);
  ASSERT(GAMESTATE != nullptr);
  ASSERT(ANNOUNCER != nullptr);
  ASSERT(THEME != nullptr);

  GAMESTATE->SetCurGame(g);

  std::string sAnnouncer = PREFSMAN->m_sAnnouncer;
  std::string sTheme = PREFSMAN->m_sTheme;
  std::string sGametype = GAMESTATE->GetCurrentGame()->m_szName;
  std::string sLanguage = PREFSMAN->m_sLanguage;

  if (sAnnouncer.empty()) {
    sAnnouncer = GAMESTATE->GetCurrentGame()->m_szName;
  }
  std::string argCurGame;
  if (GetCommandlineArgument("game", &argCurGame) && argCurGame != sGametype) {
    const Game* new_game = GAMEMAN->StringToGame(argCurGame);
    if (new_game == nullptr) {
      LOG->Warn("%s is not a known game type, ignoring.", argCurGame.c_str());
    } else {
      PREFSMAN->SetCurrentGame(sGametype);
      GAMESTATE->SetCurGame(new_game);
    }
  }

  // It doesn't matter if sTheme is blank or invalid, THEME->STAL will set
  // a selectable theme for us. -Kyz

  // process gametype, theme and language command line arguments;
  // these change the preferences in order for transparent loading -aj
  std::string argTheme;
  if (GetCommandlineArgument("theme", &argTheme) && argTheme != sTheme) {
    sTheme = argTheme;
    // set theme in preferences too for correct behavior  -aj
    PREFSMAN->m_sTheme.Set(sTheme);
  }

  std::string argLanguage;
  if (GetCommandlineArgument("language", &argLanguage)) {
    sLanguage = argLanguage;
    // set language in preferences too for correct behavior -aj
    PREFSMAN->m_sLanguage.Set(sLanguage);
  }

  // it's OK to call these functions with names that don't exist.
  ANNOUNCER->SwitchAnnouncer(sAnnouncer);
  THEME->SwitchThemeAndLanguage(sTheme, sLanguage, PREFSMAN->m_bPseudoLocalize);

  // Set the input scheme for the new game, and load keymaps.
  if (INPUTMAPPER) {
    INPUTMAPPER->SetInputScheme(&g->m_InputScheme);
    INPUTMAPPER->ReadMappingsFromDisk();
  }
}

static void ShutdownGame() {
  if (SOUNDMAN) {
    SOUNDMAN->Shutdown();
  }

  if (LIGHTSMAN) {
    LIGHTSMAN->TurnOffAllLights();
  }

  RageUtil::SafeDelete(NETWORK);
  RageUtil::SafeDelete(SCREENMAN);
  RageUtil::SafeDelete(STATSMAN);
  RageUtil::SafeDelete(MESSAGEMAN);
  RageUtil::SafeDelete(INPUTMAN);
  RageUtil::SafeDelete(INPUTQUEUE);
  RageUtil::SafeDelete(INPUTMAPPER);
  RageUtil::SafeDelete(INPUTFILTER);
  RageUtil::SafeDelete(MODELMAN);
  RageUtil::SafeDelete(PROFILEMAN);
  RageUtil::SafeDelete(CHARMAN);
  RageUtil::SafeDelete(UNLOCKMAN);
  RageUtil::SafeDelete(CRYPTMAN);
  RageUtil::SafeDelete(MEMCARDMAN);
  RageUtil::SafeDelete(SONGMAN);
  RageUtil::SafeDelete(IMAGECACHE);
  RageUtil::SafeDelete(SONGINDEX);
  RageUtil::SafeDelete(SOUND);
  RageUtil::SafeDelete(PREFSMAN);
  RageUtil::SafeDelete(GAMESTATE);
  RageUtil::SafeDelete(GAMEMAN);
  RageUtil::SafeDelete(NOTESKIN);
  RageUtil::SafeDelete(THEME);
  RageUtil::SafeDelete(ANNOUNCER);
  RageUtil::SafeDelete(BOOKKEEPER);
  RageUtil::SafeDelete(LIGHTSMAN);
  RageUtil::SafeDelete(SOUNDMAN);
  RageUtil::SafeDelete(FONT);
  RageUtil::SafeDelete(TEXTUREMAN);
  RageUtil::SafeDelete(DISPLAY);
  Dialog::Shutdown();
  RageUtil::SafeDelete(LUADEBUG);
  RageUtil::SafeDelete(LOG);
  RageUtil::SafeDelete(FILEMAN);
  RageUtil::SafeDelete(LUA);
  RageUtil::SafeDelete(HOOKS);
}

static void HandleException(const std::string& sError) {
  if (g_bAutoRestart) {
    HOOKS->RestartProgram();
  }

  RageUtil::SafeDelete(g_pLoadingWindow);
  ShutdownGame();

  Dialog::Error(sError);
  Dialog::Shutdown();
}

struct VideoCardDefaults {
  std::string sDriverRegex;
  std::string sVideoRenderers;
  int iWidth;
  int iHeight;
  int iDisplayColor;
  int iTextureColor;
  int iMovieColor;
  int iTextureSize;
  bool bSmoothLines;

  VideoCardDefaults()
      : iWidth(0),
        iHeight(0),
        iDisplayColor(0),
        iTextureColor(0),
        iMovieColor(0),
        iTextureSize(0),
        bSmoothLines(false) {}
  VideoCardDefaults(
      std::string sDriverRegex_, std::string sVideoRenderers_, int iWidth_,
      int iHeight_, int iDisplayColor_, int iTextureColor_, int iMovieColor_,
      int iTextureSize_, bool bSmoothLines_) {
    sDriverRegex = sDriverRegex_;
    sVideoRenderers = sVideoRenderers_;
    iWidth = iWidth_;
    iHeight = iHeight_;
    iDisplayColor = iDisplayColor_;
    iTextureColor = iTextureColor_;
    iMovieColor = iMovieColor_;
    iTextureSize = iTextureSize_;
    bSmoothLines = bSmoothLines_;
  }
} const g_VideoCardDefaults[] = {
    VideoCardDefaults("OpenGL", "opengl", 1280, 720, 32, 32, 32, 2048, true),
    VideoCardDefaults("", "opengl,d3d", 1280, 720, 32, 32, 32, 2048, true),
};

static std::string GetVideoDriverName() {
#if defined(_WIN32)
  return GetPrimaryVideoDriverName();
#else
  return "OpenGL";
#endif
}

bool CheckVideoDefaultSettings() {
  std::string sVideoDriver = GetVideoDriverName();

  LOG->Trace(
      "Last seen video driver: %s",
      PREFSMAN->m_sLastSeenVideoDriver.Get().c_str());

  VideoCardDefaults defaults;

  unsigned i;
  for (i = 0; i < ARRAYLEN(g_VideoCardDefaults); i++) {
    defaults = g_VideoCardDefaults[i];

    std::string sDriverRegex = defaults.sDriverRegex;
    Regex regex(sDriverRegex);
    if (regex.Compare(sVideoDriver)) {
      LOG->Trace(
          "Card matches '%s'.",
          sDriverRegex.size() ? sDriverRegex.c_str() : "(unknown card)");
      break;
    }
  }
  if (i >= ARRAYLEN(g_VideoCardDefaults)) {
    FAIL_M("Failed to match video driver");
  }

  bool bSetDefaultVideoParams = false;
  if (PREFSMAN->m_sVideoRenderers.Get() == "") {
    bSetDefaultVideoParams = true;
    LOG->Trace("Applying defaults for %s.", sVideoDriver.c_str());
  } else if (PREFSMAN->m_sLastSeenVideoDriver.Get() != sVideoDriver) {
    bSetDefaultVideoParams = true;
    LOG->Trace(
        "Video card has changed from %s to %s.  Applying new defaults.",
        PREFSMAN->m_sLastSeenVideoDriver.Get().c_str(), sVideoDriver.c_str());
  }

  if (bSetDefaultVideoParams) {
    PREFSMAN->m_sVideoRenderers.Set(defaults.sVideoRenderers);
    PREFSMAN->m_iDisplayWidth.Set(defaults.iWidth);
    PREFSMAN->m_iDisplayHeight.Set(defaults.iHeight);
    PREFSMAN->m_iDisplayColorDepth.Set(defaults.iDisplayColor);
    PREFSMAN->m_iTextureColorDepth.Set(defaults.iTextureColor);
    PREFSMAN->m_iMovieColorDepth.Set(defaults.iMovieColor);
    PREFSMAN->m_iMaxTextureResolution.Set(defaults.iTextureSize);
    PREFSMAN->m_bSmoothLines.Set(defaults.bSmoothLines);
    PREFSMAN->m_fDisplayAspectRatio.Set(PREFSMAN->m_fDisplayAspectRatio);
    PREFSMAN->m_sLastSeenVideoDriver.Set(GetVideoDriverName());
  } else if (
      CompareNoCase(
          PREFSMAN->m_sVideoRenderers.Get(), defaults.sVideoRenderers)) {
    LOG->Warn(
        "Video renderer list has been changed from '%s' to '%s'",
        defaults.sVideoRenderers.c_str(),
        PREFSMAN->m_sVideoRenderers.Get().c_str());
  }

  LOG->Info("Video renderers: '%s'", PREFSMAN->m_sVideoRenderers.Get().c_str());
  return bSetDefaultVideoParams;
}

RageDisplay* CreateDisplay() {
  CheckVideoDefaultSettings();

  VideoModeParams params;
  StepMania::GetPreferredVideoModeParams(params);

  std::string error =
      ERROR_INITIALIZING_CARD.GetValue() + "\n\n" +
      ERROR_DONT_FILE_BUG.GetValue() + "\n\n" VIDEO_TROUBLESHOOTING_URL "\n\n" +
      ssprintf(
          ERROR_VIDEO_DRIVER.GetValue().c_str(), GetVideoDriverName().c_str()) +
      "\n\n";

  std::vector<std::string> asRenderers;
  split(PREFSMAN->m_sVideoRenderers, ",", asRenderers, true);

  if (asRenderers.empty()) {
    RageException::Throw("%s", ERROR_NO_VIDEO_RENDERERS.GetValue().c_str());
  }

  RageDisplay* pRet = nullptr;
  for (unsigned i = 0; i < asRenderers.size(); i++) {
    std::string sRenderer = asRenderers[i];

    if (CompareNoCase(sRenderer, "opengl") == 0) {
#if defined(SUPPORT_OPENGL)
      pRet = new RageDisplay_Legacy;
#endif
    } else if (CompareNoCase(sRenderer, "gles2") == 0) {
#if defined(SUPPORT_GLES2)
      pRet = new RageDisplay_GLES2;
#endif
    } else if (CompareNoCase(sRenderer, "d3d") == 0) {
#if defined(SUPPORT_D3D)
      pRet = new RageDisplay_D3D;
#endif
    } else if (CompareNoCase(sRenderer, "null") == 0) {
      return new RageDisplay_Null;
    } else {
      RageException::Throw(
          ERROR_UNKNOWN_VIDEO_RENDERER.GetValue().c_str(), sRenderer.c_str());
    }

    if (pRet == nullptr) {
      continue;
    }

    std::string sError =
        pRet->Init(params, PREFSMAN->m_bAllowUnacceleratedRenderer);
    if (!sError.empty()) {
      error +=
          ssprintf(ERROR_INITIALIZING.GetValue().c_str(), sRenderer.c_str()) +
          "\n" + sError;
      RageUtil::SafeDelete(pRet);
      error += "\n\n\n";
      continue;
    }

    break;
  }

  if (pRet == nullptr) {
    RageException::Throw("%s", error.c_str());
  }

  return pRet;
}

static void SwitchToLastPlayedGame() {
  const Game* pGame = GAMEMAN->StringToGame(PREFSMAN->GetCurrentGame());

  if (pGame == nullptr) {
    pGame = GAMEMAN->GetDefaultGame();
  }

  if (!GAMEMAN->IsGameEnabled(pGame) && pGame != GAMEMAN->GetDefaultGame()) {
    pGame = GAMEMAN->GetDefaultGame();
    LOG->Warn(
        "Default NoteSkin for \"%s\" missing, reverting to \"%s\"",
        pGame->m_szName, GAMEMAN->GetDefaultGame()->m_szName);
  }

  ASSERT(GAMEMAN->IsGameEnabled(pGame));

  StepMania::InitializeCurrentGame(pGame);
}

static void MountTreeOfZips(const std::string& dir) {
  std::vector<std::string> dirs;
  dirs.push_back(dir);

  while (dirs.size()) {
    std::string path = dirs.back();
    dirs.pop_back();

    if (!IsADirectory(path)) {
      continue;
    }

    std::vector<std::string> zips;
    GetDirListing(path + "/*.zip", zips, false, true);
    GetDirListing(path + "/*.smzip", zips, false, true);

    for (const auto& zip : zips) {
      if (!IsAFile(zip)) {
        continue;
      }

      LOG->Trace("VFS: found %s", zip.c_str());
      FILEMAN->Mount("zip", zip, "/");
    }

    GetDirListing(path + "/*", dirs, true, true);
  }
}

static void MountFolders(
    const std::string& type, const std::string& realPathList,
    const std::string& mountPoint) {
  if (!realPathList.empty()) {
    std::vector<std::string> dirs;
    split(realPathList, ",", dirs, true);
    for (const auto& dir : dirs) {
      FILEMAN->Mount(type, dir, mountPoint);
    }
  }
}

static void WriteLogHeader() {
  LOG->Info("%s%s", PRODUCT_FAMILY, product_version);

  LOG->Info(
      "Compiled %s @ %s (build %s)", version_date, version_time,
      ::sm_version_git_hash);

  time_t cur_time;
  time(&cur_time);
  struct tm now;
  localtime_r(&cur_time, &now);

  LOG->Info(
      "Log starting %.4d-%.2d-%.2d %.2d:%.2d:%.2d", 1900 + now.tm_year,
      now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
  LOG->Trace(" ");

  if (g_argc > 1) {
    std::string args;
    for (int i = 1; i < g_argc; ++i) {
      if (i > 1) {
        args += " ";
      }

      args += ssprintf("[[%s]]", g_argv[i]);
    }
    LOG->Info("Command line args (count=%d): %s", (g_argc - 1), args.c_str());
  }
}

static void ApplyLogPreferences() {
  LOG->SetShowLogOutput(PREFSMAN->m_bShowLogOutput);
  LOG->SetLogToDisk(PREFSMAN->m_bLogToDisk);
  LOG->SetInfoToDisk(PREFSMAN->m_bLogToDisk);
  LOG->SetUserLogToDisk(PREFSMAN->m_bLogToDisk);
  LOG->SetFlushing(PREFSMAN->m_bForceLogFlush);
  Checkpoints::LogCheckpoints(PREFSMAN->m_bLogCheckpoints);
}

int Bootstrap::Run(int argc, char* argv[]) {
  RageThreadRegister thread("Main thread");
  RageException::SetCleanupHandler(HandleException);

  SetCommandlineArguments(argc, argv);
  HOOKS = ArchHooks::Create();
  HOOKS->Init();
  LUA = new LuaManager;
  HOOKS->RegisterWithLua();
  ActorUtil::InitFileTypeLists();
  FILEMAN = new RageFileManager(argv[0]);
  FILEMAN->ProtectPath(SpecialFiles::DEFAULTS_INI_PATH);
  FILEMAN->ProtectPath(SpecialFiles::STATIC_INI_PATH);
  FILEMAN->ProtectPath(SpecialFiles::PREFERENCES_INI_PATH);
  FILEMAN->MountInitialFilesystems();
  bool bPortable = DoesFileExist("/Portable.ini");
  if (!bPortable) {
    FILEMAN->MountUserFilesystems();
  }
  LOG = new RageLog;
  PREFSMAN = new PrefsManager;
  if (!g_bAllowMultipleInstances.Get() &&
      HOOKS->CheckForMultipleInstances(argc, argv)) {
    ShutdownGame();
    return 0;
  }

  ApplyLogPreferences();
  WriteLogHeader();
  MountFolders("dirro", PREFSMAN->m_sAdditionalFoldersReadOnly.Get(), "/");
  MountFolders("dir", PREFSMAN->m_sAdditionalFoldersWritable.Get(), "/");
  MountFolders(
      "dirro", PREFSMAN->m_sAdditionalSongFoldersReadOnly.Get(), "/Songs");
  MountFolders(
      "dir", PREFSMAN->m_sAdditionalSongFoldersWritable.Get(), "/Songs");
  MountFolders(
      "dirro", PREFSMAN->m_sAdditionalCourseFoldersReadOnly.Get(), "/Courses");
  MountFolders(
      "dir", PREFSMAN->m_sAdditionalCourseFoldersWritable.Get(), "/Courses");
  MountTreeOfZips(SpecialFiles::PACKAGES_DIR);
  PREFSMAN->ReadPrefsFromDisk();
  ApplyLogPreferences();
  Dialog::Init();
  MESSAGEMAN = new MessageManager;
  GAMESTATE = new GameState;
  g_pLoadingWindow = LoadingWindow::Create();
  if (g_pLoadingWindow == nullptr) {
    RageException::Throw("%s", COULDNT_OPEN_LOADING_WINDOW.GetValue().c_str());
  }
  srand(time(nullptr));
  HOOKS->DumpDebugInfo();
#if defined(HAVE_TLS)
  LOG->Info("TLS is %savailable", RageThread::GetSupportsTLS() ? "" : "not ");
#endif
  std::string luaDebugServerAddress;
  if (GetCommandlineArgument("lua-debugger", &luaDebugServerAddress)) {
    LUADEBUG = new LuaDebugManager();
    LUADEBUG->Start(
        luaDebugServerAddress, GetCommandlineArgument("lua-debugger-paused"));
  }

  GAMEMAN = new GameManager;
  THEME = new ThemeManager;
  ANNOUNCER = new AnnouncerManager;
  NOTESKIN = new NoteSkinManager;
  SwitchToLastPlayedGame();
  CommandLineActions::Handle(g_pLoadingWindow);
  if (GetCommandlineArgument("dopefish")) {
    GAMESTATE->m_bDopefish = true;
  }
  if (g_pLoadingWindow != nullptr) {
    std::string sError;
    RageSurface* pSurface = RageSurfaceUtils::LoadFile(
        THEME->GetPathG("Common", "window icon"), sError);
    if (pSurface != nullptr) {
      g_pLoadingWindow->SetIcon(pSurface);
    }
    delete pSurface;
    pSurface =
        RageSurfaceUtils::LoadFile(THEME->GetPathG("Common", "splash"), sError);
    if (pSurface != nullptr) {
      g_pLoadingWindow->SetSplash(pSurface);
    }
    delete pSurface;
  }
  if (PREFSMAN->m_iSoundWriteAhead) {
    LOG->Info(
        "Sound writeahead has been overridden to %i",
        PREFSMAN->m_iSoundWriteAhead.Get());
  }
  SOUNDMAN = new RageSoundManager;
  SOUNDMAN->Init();
  SOUNDMAN->SetMixVolume();
  SOUND = new GameSoundManager;
  BOOKKEEPER = new Bookkeeper;
  LIGHTSMAN = new LightsManager;
  INPUTFILTER = new InputFilter;
  INPUTMAPPER = new InputMapper;
  StepMania::InitializeCurrentGame(GAMESTATE->GetCurrentGame());
  INPUTQUEUE = new InputQueue;
  SONGINDEX = new SongCacheIndex;
  IMAGECACHE = new ImageCache;
  SONGMAN = new SongManager;
  SONGMAN->InitAll(g_pLoadingWindow, /*onlyAdditions=*/false);
  CRYPTMAN = new CryptManager;
  if (PREFSMAN->m_bSignProfileData) {
    CRYPTMAN->GenerateGlobalKeys();
  }
  MEMCARDMAN = new MemoryCardManager;
  CHARMAN = new CharacterManager;
  PROFILEMAN = new ProfileManager;
  PROFILEMAN->Init();
  UNLOCKMAN = new UnlockManager;
  SONGMAN->UpdatePopular();
  SONGMAN->UpdatePreferredSort();
  NETWORK = new NetworkManager;
  STATSMAN = new StatsManager;
  SONGMAN->UpdateRankingCourses();
  RageUtil::SafeDelete(g_pLoadingWindow);

  if (ArchHooks::UserQuit()) {
    ShutdownGame();
    return 0;
  }

  StartDisplay();
  StoreActualGraphicOptions();
  LOG->Info("%s", GetActualGraphicOptionsString().c_str());
  SONGMAN->PreloadSongImages();
  INPUTMAN = new RageInput;
  FONT = new FontManager;
  SCREENMAN = new ScreenManager;
  StepMania::ResetGame();
  SCREENMAN->ThemeChanged();
  SCREENMAN->SetNewScreen(StepMania::GetInitialScreen());
  std::string sMessage;
  if (INPUTMAPPER->CheckForChangedInputDevicesAndRemap(sMessage)) {
    SCREENMAN->SystemMessage(sMessage);
  }
  CodeDetector::RefreshCacheItems();

  GameLoop::RunGameLoop();

  PREFSMAN->SavePrefsToDisk();

  ShutdownGame();
  return 0;
}

namespace StepMania::General {
static int Run(int argc, char* argv[]) {
  Bootstrap bootstrap;
  return bootstrap.Run(argc, argv);
}
}  // namespace StepMania::General

int sm_main(int argc, char* argv[]) {
  return StepMania::General::Run(argc, argv);
}

static int LuaFunc_update_centering(lua_State* L) {
  update_centering();
  return 0;
}
LUAFUNC_REGISTER_COMMON(update_centering);

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
