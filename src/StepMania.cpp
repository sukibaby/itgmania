/* Functions which run on a per-frame basis, or are otherwise time sensitive. */

#include "StepMania.h"

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageException.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "StdString.h"
#include "ThemeMetric.h"
#include "global.h"

// Rage global classes
#include "CodeDetector.h"
#include "CommonMetrics.h"
#include "Game.h"
#include "GameSoundManager.h"
#include "InputEventPlus.h"
#include "LocalizedString.h"
#include "ProductInfo.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSurface.h"
#include "RageSurface_Load.h"
#include "RageTextureManager.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil/Regex.h"
#include "Screen.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/Dialog/Dialog.h"
#include "arch/LoadingWindow/LoadingWindow.h"

#if !defined(SUPPORT_OPENGL) && !defined(SUPPORT_D3D)
#define SUPPORT_OPENGL
#endif

// StepMania global classes
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "CharacterManager.h"
#include "FontManager.h"
#include "GameLoop.h"
#include "GameState.h"
#include "ImageCache.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "InputQueue.h"
#include "LuaDebugManager.h"
#include "MessageManager.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include "NoteSkinManager.h"
#include "PrefsManager.h"
#include "RageFileManager.h"
#include "ScreenManager.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "SpecialFiles.h"
#include "StatsManager.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "ver.h"

bool HandleGlobalInputs(const InputEventPlus& input);
void HandleInputEvents(float fDeltaTime);

void StepMania::ResetGame() {
  GAMESTATE->Reset();

  if (!THEME->DoesThemeExist(THEME->GetCurThemeName())) {
    std::string sGameName = GAMESTATE->GetCurrentGame()->m_szName;
    if (!THEME->DoesThemeExist(sGameName)) {
      sGameName = PREFSMAN->m_sDefaultTheme;  // was previously "default" -aj
    }
    THEME->SwitchThemeAndLanguage(
        sGameName, THEME->GetCurLanguage(), PREFSMAN->m_bPseudoLocalize);
    TEXTUREMAN->DoDelayedDelete();
  }

  PREFSMAN->SavePrefsToDisk();
}

ThemeMetric<std::string> INITIAL_SCREEN("Common", "InitialScreen");
std::string StepMania::GetInitialScreen() {
  if (PREFSMAN->m_sTestInitialScreen.Get() != "" &&
      SCREENMAN->IsScreenNameValid(PREFSMAN->m_sTestInitialScreen)) {
    return PREFSMAN->m_sTestInitialScreen;
  }
  std::string screen_name = INITIAL_SCREEN.GetValue();
  if (!SCREENMAN->IsScreenNameValid(screen_name)) {
    screen_name = "ScreenInitialScreenIsInvalid";
  }
  return screen_name;
}
ThemeMetric<std::string> SELECT_MUSIC_SCREEN("Common", "SelectMusicScreen");
std::string StepMania::GetSelectMusicScreen() {
  return SELECT_MUSIC_SCREEN.GetValue();
}

/* Returns true if the key has been handled and should be discarded, false if
 * the key should be sent on to screens. */
static LocalizedString SERVICE_SWITCH_PRESSED(
    "StepMania", "Service switch pressed");
static LocalizedString RELOADED_METRICS("ThemeManager", "Reloaded metrics");
static LocalizedString RELOADED_METRICS_AND_TEXTURES(
    "ThemeManager", "Reloaded metrics and textures");
static LocalizedString RELOADED_SCRIPTS("ThemeManager", "Reloaded scripts");
static LocalizedString RELOADED_OVERLAY_SCREENS(
    "ThemeManager", "Reloaded overlay screens");
bool HandleGlobalInputs(const InputEventPlus& input) {
  // None of the globals keys act on types other than FIRST_PRESS
  if (input.type != IET_FIRST_PRESS) {
    return false;
  }

  switch (input.MenuI) {
    case GAME_BUTTON_OPERATOR:
      /* Global operator key, to get quick access to the options menu. Don't
       * do this if we're on a "system menu", which includes the editor
       * (to prevent quitting without storing changes). */
      if (SCREENMAN->AllowOperatorMenuButton()) {
        SCREENMAN->SystemMessage(SERVICE_SWITCH_PRESSED);
        SCREENMAN->PopAllScreens();
        GAMESTATE->Reset();
        SCREENMAN->SetNewScreen(CommonMetrics::OPERATOR_MENU_SCREEN);
      }
      return true;

    case GAME_BUTTON_COIN:
      // Handle a coin insertion.
      if (GAMESTATE->IsEditing())  // no coins while editing
      {
        LOG->Trace("Ignored coin insertion (editing)");
        break;
      }
      StepMania::InsertCoin();
      return false;  // Attract needs to know because it goes to TitleMenu on >
                     // 1 credit
    default:
      break;
  }

  /* Re-added for StepMania 3.9 theming veterans, plus it's just faster than
   * the debug menu. The Shift button only reloads the metrics, unlike in 3.9
   * (where it saved bookkeeping and machine profile). -aj */
  bool bIsShiftHeld =
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT), &input.InputList) ||
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT), &input.InputList);
  bool bIsCtrlHeld =
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL), &input.InputList) ||
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL), &input.InputList);
  if (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_F2)) {
    if (bIsShiftHeld && !bIsCtrlHeld) {
      // Shift+F2: refresh metrics,noteskin cache and CodeDetector cache only
      THEME->ReloadMetrics();
      NOTESKIN->RefreshNoteSkinData(GAMESTATE->m_pCurGame);
      CodeDetector::RefreshCacheItems();
      SCREENMAN->SystemMessage(RELOADED_METRICS);
    } else if (bIsCtrlHeld && !bIsShiftHeld) {
      // Ctrl+F2: reload scripts only
      if (NETWORK != nullptr) {
        NETWORK->CloseAllWebSockets();
      }
      THEME->UpdateLuaGlobals();
      SCREENMAN->SystemMessage(RELOADED_SCRIPTS);
    } else if (bIsCtrlHeld && bIsShiftHeld) {
      // Shift+Ctrl+F2: reload overlay screens (and metrics, since themers
      // are likely going to do this after changing metrics.)
      THEME->ReloadMetrics();
      SCREENMAN->ReloadOverlayScreens();
      SCREENMAN->SystemMessage(RELOADED_OVERLAY_SCREENS);
    } else {
      // F2 alone: refresh metrics, textures, noteskins, codedetector cache
      THEME->ReloadMetrics();
      TEXTUREMAN->ReloadAll();
      NOTESKIN->RefreshNoteSkinData(GAMESTATE->m_pCurGame);
      CodeDetector::RefreshCacheItems();
      SCREENMAN->SystemMessage(RELOADED_METRICS_AND_TEXTURES);
    }

    return true;
  }

  if (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_PAUSE)) {
    Message msg("ToggleConsoleDisplay");
    MESSAGEMAN->Broadcast(msg);
    return true;
  }

#if !defined(MACOSX)
  if (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_F4)) {
    if (INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.InputList) ||
        INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.InputList)) {
      // pressed Alt+F4
      ArchHooks::SetUserQuit();
      return true;
    }
  }
#else
  if (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_Cq) &&
      (INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_LMETA), &input.InputList) ||
       INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_RMETA), &input.InputList))) {
    /* The user quit is handled by the menu item so we don't need to set it
     * here; however, we do want to return that it has been handled since
     * this will happen first. */
    return true;
  }
#endif

  bool bDoScreenshot =
#if defined(MACOSX)
      // Notebooks don't have F13. Use cmd-F12 as well.
      input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_PRTSC) ||
      input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_F13) ||
      (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_F12) &&
       (INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_LMETA), &input.InputList) ||
        INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_RMETA), &input.InputList)));
#else
      /* The default Windows message handler will capture the desktop window
       * upon pressing PrntScrn, or will capture the foreground with focus upon
       * pressing Alt+PrntScrn. Windows will do this whether or not we save a
       * screenshot ourself by dumping the frame buffer. */
      // "if pressing PrintScreen and not pressing Alt"
      input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_PRTSC) &&
      !INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.InputList) &&
      !INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.InputList);
#endif
  if (bDoScreenshot) {
    // If holding Shift save uncompressed, else save compressed
    bool bHoldingShift =
        (INPUTFILTER->IsBeingPressed(
             DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
         INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT)));
    bool bSaveCompressed = !bHoldingShift;
    RageTimer timer;
    StepMania::SaveScreenshot("Screenshots/", bSaveCompressed, false, "", "");
    LOG->Trace("Screenshot took %f seconds.", timer.GetDeltaTime());
    return true;  // handled
  }

  if (input.DeviceI == DeviceInput(DEVICE_KEYBOARD, KEY_ENTER) &&
      (INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.InputList) ||
       INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.InputList))) {
    // alt-enter
    /* In macOS, this is a menu item and will be handled as such. This will
     * happen first and then the lower priority GUI thread will happen second,
     * causing the window to toggle twice. Another solution would be to put
     * a timer in ArchHooks::SetToggleWindowed() and just not set the bool
     * it if it's been less than, say, half a second. */
#if !defined(MACOSX)
    ArchHooks::SetToggleWindowed();
#endif
    return true;
  }

  return false;
}

void HandleInputEvents(float fDeltaTime) {
  INPUTFILTER->Update(fDeltaTime);

  /* Hack: If the topmost screen hasn't been updated yet, don't process input,
   * since we must not send inputs to a screen that hasn't at least had one
   * update yet. (The first Update should be the very first thing a screen
   * gets.) We'll process it next time. Call Update above, so the inputs are
   * read and timestamped. */
  if (SCREENMAN->GetTopScreen()->IsFirstUpdate()) {
    return;
  }

  std::vector<InputEvent> ieArray;
  INPUTFILTER->GetInputEvents(ieArray);

  // If we don't have focus, discard input.
  if (!HOOKS->AppHasFocus()) {
    return;
  }

  if (!ieArray.empty()) {
    GameLoop::ResetInputIdleTimer();
  }

  for (unsigned i = 0; i < ieArray.size(); i++) {
    InputEventPlus input;
    input.DeviceI = ieArray[i].di;
    input.type = ieArray[i].type;
    swap(input.InputList, ieArray[i].m_ButtonState);

    // hack for testing (MultiPlayer) with only one joystick
    /*
    if( input.DeviceI.IsJoystick() )
    {
            if( INPUTFILTER->IsBeingPressed(
    DeviceInput(DEVICE_KEYBOARD,KEY_LSHIFT) ) ) input.DeviceI.device =
    (InputDevice)(input.DeviceI.device + 1); if( INPUTFILTER->IsBeingPressed(
    DeviceInput(DEVICE_KEYBOARD,KEY_LCTRL) ) ) input.DeviceI.device =
    (InputDevice)(input.DeviceI.device + 2); if( INPUTFILTER->IsBeingPressed(
    DeviceInput(DEVICE_KEYBOARD,KEY_LALT) ) ) input.DeviceI.device =
    (InputDevice)(input.DeviceI.device + 4); if( INPUTFILTER->IsBeingPressed(
    DeviceInput(DEVICE_KEYBOARD,KEY_RALT) ) ) input.DeviceI.device =
    (InputDevice)(input.DeviceI.device + 8); if( INPUTFILTER->IsBeingPressed(
    DeviceInput(DEVICE_KEYBOARD,KEY_RCTRL) ) ) input.DeviceI.device =
    (InputDevice)(input.DeviceI.device + 16);
    }
    */

    INPUTMAPPER->DeviceToGame(input.DeviceI, input.GameI);

    input.mp = MultiPlayer_Invalid;

    {
      // Translate input to the appropriate MultiPlayer. Assume that all
      // joystick devices are mapped the same as the master player.
      if (input.DeviceI.IsJoystick()) {
        DeviceInput diTemp = input.DeviceI;
        diTemp.device = DEVICE_JOY1;
        GameInput gi;

        // LOG->Trace( "device %d, %d", diTemp.device, diTemp.button );
        if (INPUTMAPPER->DeviceToGame(diTemp, gi)) {
          if (GAMESTATE->m_bMultiplayer) {
            input.GameI = gi;
            // LOG->Trace( "game %d %d", input.GameI.controller,
            // input.GameI.button );
          }

          input.mp =
              InputMapper::InputDeviceToMultiPlayer(input.DeviceI.device);
          // LOG->Trace( "multiplayer %d", input.mp );
          ASSERT(input.mp >= 0 && input.mp < NUM_MultiPlayer);
        }
      }
    }

    if (input.GameI.IsValid()) {
      input.MenuI = INPUTMAPPER->GameButtonToMenuButton(input.GameI.button);
      input.pn = INPUTMAPPER->ControllerToPlayerNumber(input.GameI.controller);
    }

    INPUTQUEUE->RememberInput(input);

    // When a GameButton is pressed, stop repeating other keys on the same
    // controller.
    if (input.type == IET_FIRST_PRESS && input.MenuI != GameButton_Invalid) {
      FOREACH_ENUM(GameButton, m) {
        if (input.MenuI != m) {
          INPUTMAPPER->RepeatStopKey(m, input.pn);
        }
      }
    }

    if (HandleGlobalInputs(input)) {
      continue;  // skip
    }

    // check back in event mode
    if (GAMESTATE->IsEventMode() &&
        CodeDetector::EnteredCode(
            input.GameI.controller, CODE_BACK_IN_EVENT_MODE)) {
      input.MenuI = GAME_BUTTON_BACK;
    }

    SCREENMAN->Input(input);
  }

  if (ArchHooks::GetAndClearToggleWindowed()) {
    PREFSMAN->m_bWindowed.Set(!PREFSMAN->m_bWindowed);
    StepMania::ApplyGraphicOptions();
  }
}

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
