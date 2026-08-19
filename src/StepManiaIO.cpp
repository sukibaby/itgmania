/* Blocking functions involving disk I/O. */

#include <string>

#include "Bookkeeper.h"
#include "CryptManager.h"
#include "DateTime.h"
#include "GameState.h"
#include "LightsManager.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "ScreenManager.h"
#include "StdString.h"
#include "StepMania.h"
#include "global.h"

std::string StepMania::SaveScreenshot(
    std::string Dir, bool SaveCompressed, bool MakeSignature,
    std::string NamePrefix, std::string NameSuffix) {
  /* As of sm-ssc v1.0 rc2, screenshots are no longer named by an arbitrary
   * index. This was causing naming issues for some unknown reason, so we have
   * changed the screenshot names to a non-blocking format: date and time.
   * As before, we ignore the extension. -aj */
  std::string FileNameNoExtension =
      NamePrefix + DateTime::GetNowDateTime().GetString() + NameSuffix;
  // replace space with underscore.
  Replace(FileNameNoExtension, " ", "_");
  // colons are illegal in filenames.
  Replace(FileNameNoExtension, ":", "");

  // Save the screenshot. If writing lossy to a memcard, use
  // SAVE_LOSSY_LOW_QUAL, so we don't eat up lots of space.
  RageDisplay::GraphicsFileFormat fmt;
  if (SaveCompressed) {
    fmt = RageDisplay::SAVE_LOSSY_HIGH_QUAL;
  } else {
    fmt = RageDisplay::SAVE_LOSSLESS_SENSIBLE;
  }

  std::string FileName =
      FileNameNoExtension + "." + (SaveCompressed ? "jpg" : "png");
  std::string Path = Dir + FileName;

  if (!DISPLAY->SaveScreenshot(Path, fmt)) {
    SCREENMAN->PlayInvalidSound();
    return std::string();
  }

  SCREENMAN->PlayScreenshotSound();

  if (PREFSMAN->m_bSignProfileData && MakeSignature) {
    CryptManager::SignFileToFile(Path);
  }

  return FileName;
}

void StepMania::InsertCoin(int iNum, bool bCountInBookkeeping) {
  if (bCountInBookkeeping) {
    LIGHTSMAN->PulseCoinCounter();
    BOOKKEEPER->CoinInserted();
  }
  int iNumCoinsOld = GAMESTATE->m_iCoins;

  // Don't allow GAMESTATE's coin count to become negative.
  if (GAMESTATE->m_iCoins + iNum >= 0) {
    GAMESTATE->m_iCoins.Set(GAMESTATE->m_iCoins + iNum);
  }

  int iCredits = GAMESTATE->m_iCoins / PREFSMAN->m_iCoinsPerCredit;
  bool bMaxCredits = iCredits >= PREFSMAN->m_iMaxNumCredits;
  if (bMaxCredits) {
    GAMESTATE->m_iCoins.Set(
        PREFSMAN->m_iMaxNumCredits * PREFSMAN->m_iCoinsPerCredit);
  }

  LOG->Trace(
      "%i coins inserted, %i needed to play", GAMESTATE->m_iCoins.Get(),
      PREFSMAN->m_iCoinsPerCredit.Get());

  // On InsertCoin, make sure to update Coins file
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->m_iCoins.Get());

  // If inserting coins, play an appropriate sound; if deducting coins, don't
  // play anything.
  if (iNum > 0) {
    if (iNumCoinsOld != GAMESTATE->m_iCoins) {
      SCREENMAN->PlayCoinSound();
    } else {
      SCREENMAN->PlayInvalidSound();
    }
  }

  /* If AutoJoin and a player is already joined, then try to join a player.
   * (If no players are joined, they'll join on the first JoinInput.) */
  if (GAMESTATE->m_bAutoJoin.Get() && GAMESTATE->GetNumSidesJoined() > 0) {
    if (GAMESTATE->GetNumSidesJoined() > 0 && GAMESTATE->JoinPlayers()) {
      SCREENMAN->PlayStartSound();
    }
  }

  // TODO: remove this redundant message and things that depend on it
  Message msg("CoinInserted");
  // below params are unused
  // msg.SetParam( "Coins", GAMESTATE->m_iCoins );
  // msg.SetParam( "Inserted", iNum );
  // msg.SetParam( "MaxCredits", bMaxCredits );
  MESSAGEMAN->Broadcast(msg);
}

void StepMania::InsertCredit() {
  InsertCoin(PREFSMAN->m_iCoinsPerCredit, false);
}

void StepMania::ClearCredits() {
  LOG->Trace("%i coins cleared", GAMESTATE->m_iCoins.Get());
  GAMESTATE->m_iCoins.Set(0);
  SCREENMAN->PlayInvalidSound();

  // Update Coins file to make sure credits are cleared.
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->m_iCoins.Get());

  // TODO: remove this redundant message and things that depend on it
  Message msg("CoinInserted");
  // below params are unused
  // msg.SetParam( "Coins", GAMESTATE->m_iCoins );
  // msg.SetParam( "Clear", true );
  MESSAGEMAN->Broadcast(msg);
}

#include "LuaManager.h"
int LuaFunc_SaveScreenshot(lua_State* L);
int LuaFunc_SaveScreenshot(lua_State* L) {
  // If pn is provided, save to that player's profile.
  // Otherwise, save to the machine.
  PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1, true);
  bool compress = lua_toboolean(L, 2) > 0;
  bool sign = lua_toboolean(L, 3) > 0;
  std::string prefix = luaL_optstring(L, 4, "");
  std::string suffix = luaL_optstring(L, 5, "");
  std::string dir;
  if (pn == PlayerNumber_Invalid) {
    dir = "Screenshots/";
  } else {
    dir = PROFILEMAN->GetProfileDir((ProfileSlot)pn) + "Screenshots/";
    if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
      MEMCARDMAN->MountCard(pn);
    }
  }
  std::string filename =
      StepMania::SaveScreenshot(dir, compress, sign, prefix, suffix);
  if (pn != PlayerNumber_Invalid) {
    if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
      MEMCARDMAN->UnmountCard(pn);
    }
  }
  std::string path = dir + filename;
  lua_pushboolean(L, !filename.empty());
  lua_pushstring(L, path.c_str());
  return 2;
}
void LuaFunc_Register_SaveScreenshot(lua_State* L);
void LuaFunc_Register_SaveScreenshot(lua_State* L) {
  lua_register(L, "SaveScreenshot", LuaFunc_SaveScreenshot);
}
REGISTER_WITH_LUA_FUNCTION(LuaFunc_Register_SaveScreenshot);

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
