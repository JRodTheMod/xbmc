/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "LibretroEnvironment.h"
#include "Application.h"
#include "dialogs/GUIDialogContextMenu.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "filesystem/Directory.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/WindowIDs.h"
#include "settings/Settings.h"
#include "storage/MediaManager.h"
#include "utils/log.h"
#include "utils/RegExp.h"
#include "utils/StringUtils.h"

#include <set>
#include <sstream>
#include <vector>

#define SETTING_TYPE_NONE            (0 << 0)
#define SETTING_TYPE_BOOL            (1 << 0)
#define SETTING_TYPE_TEXT            (1 << 1)
#define SETTING_TYPE_IPADDRESS       (1 << 2)
#define SETTING_TYPE_NUMBER          (1 << 3)
#define SETTING_TYPE_SLIDER_INT      (1 << 4)
#define SETTING_TYPE_SLIDER_FLOAT    (1 << 5)
#define SETTING_TYPE_SLIDER_PERCENT  (1 << 6)
#define SETTING_TYPE_ENUM            (1 << 7)

using namespace ADDON;
using namespace XFILE;
using namespace std;

CLibretroEnvironment::SetPixelFormat_t         CLibretroEnvironment::fn_SetPixelFormat         = NULL;
CLibretroEnvironment::SetKeyboardCallback_t    CLibretroEnvironment::fn_SetKeyboardCallback    = NULL;
CLibretroEnvironment::SetDiskControlCallback_t CLibretroEnvironment::fn_SetDiskControlCallback = NULL;
CLibretroEnvironment::SetRenderCallback_t      CLibretroEnvironment::fn_SetRenderCallback      = NULL;

GameClientPtr CLibretroEnvironment::m_activeClient;
CStdString    CLibretroEnvironment::m_systemDirectory;
std::map<CStdString, CStdString> CLibretroEnvironment::m_varMap;
bool          CLibretroEnvironment::m_bAbort = false;

void CLibretroEnvironment::SetCallbacks(SetPixelFormat_t spf, SetKeyboardCallback_t skc,
                                        SetDiskControlCallback_t sdcc, SetRenderCallback_t src,
                                        GameClientPtr activeClient)
{
  fn_SetPixelFormat = spf;
  fn_SetKeyboardCallback = skc;
  fn_SetDiskControlCallback = sdcc;
  fn_SetRenderCallback = src;
  m_activeClient = activeClient;
  m_bAbort = false;
}

void CLibretroEnvironment::ResetCallbacks()
{
  fn_SetPixelFormat = NULL;
  fn_SetKeyboardCallback = NULL;
  fn_SetDiskControlCallback = NULL;
  fn_SetRenderCallback = NULL;
  m_activeClient.reset();
}

bool CLibretroEnvironment::EnvironmentCallback(unsigned int cmd, void *data)
{
  static const char *cmds[] = {"SET_ROTATION",
                               "GET_OVERSCAN",
                               "GET_CAN_DUPE",
                               NULL, // reserved (no longer supported)
                               NULL, // reserved (no longer supported)
                               "SET_MESSAGE",
                               "SHUTDOWN",
                               "SET_PERFORMANCE_LEVEL",
                               "GET_SYSTEM_DIRECTORY",
                               "SET_PIXEL_FORMAT",
                               "SET_INPUT_DESCRIPTORS",
                               "SET_KEYBOARD_CALLBACK",
                               "SET_DISK_CONTROL_INTERFACE",
                               "SET_HW_RENDER",
                               "GET_VARIABLE",
                               "SET_VARIABLES",
                               "GET_VARIABLE_UPDATE"};

  if (0 <= cmd && cmd < sizeof(cmds) / sizeof(cmds[0]) && cmds[cmd - 1])
    CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: %s", cmd, cmds[cmd - 1]);
  else
  {
    CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: invalid query", cmd);
    return false;
  }

  // Note: SHUTDOWN doesn't use data
  if (!data && cmd != RETRO_ENVIRONMENT_SHUTDOWN)
  {
    CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: no data! naughty core?", cmd);
    return false;
  }

  switch (cmd)
  {
  case RETRO_ENVIRONMENT_GET_OVERSCAN:
    {
      // Whether or not the game client should use overscan (true) or crop away overscan (false)
      *reinterpret_cast<bool*>(data) = false;
      CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: %s", cmd,
        *reinterpret_cast<bool*>(data) ? "use overscan" : "crop away overscan");
      break;
    }
  case RETRO_ENVIRONMENT_GET_CAN_DUPE:
    {
      // Boolean value whether or not we support frame duping, passing NULL to video frame callback
      *reinterpret_cast<bool*>(data) = true;
      CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: frame duping is %s", cmd,
        *reinterpret_cast<bool*>(data) ? "enabled" : "disabled");
      break;
    }
  case RETRO_ENVIRONMENT_SET_MESSAGE:
    {
      // Sets a message to be displayed. Generally not for trivial messages.
      const retro_message *msg = reinterpret_cast<const retro_message*>(data);
      if (msg->msg && msg->frames)
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: display msg \"%s\" for %d frames", cmd, msg->msg, msg->frames);
      break;
    }
  case RETRO_ENVIRONMENT_SET_ROTATION:
    {
      // Sets screen rotation of graphics. Valid values are 0, 1, 2, 3, which rotates screen
      // by 0, 90, 180, 270 degrees counter-clockwise respectively.
      unsigned int rotation = *reinterpret_cast<const unsigned int*>(data);
      if (0 <= rotation && rotation <= 3)
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: set screen rotation to %d degrees", cmd, rotation * 90);
      else
        CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: invalid rotation %d", cmd, rotation);
      break;
    }
  case RETRO_ENVIRONMENT_SHUTDOWN:
    // Game has been shut down. Should only be used if game has a specific way to shutdown
    // the game from a menu item or similar.
    CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: game signaled shutdown event", cmd);

    g_application.StopPlaying();

    break;
  case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    {
      // Generally how computationally intense this core is, to gauge how capable XBMC's host system
      // will be for running the core. It can also be called on a game-specific basis. The levels
      // are "floating", but roughly defined as:
      // 0: Low-powered embedded devices such as Raspberry Pi
      // 1: Phones, tablets, 6th generation consoles, such as Wii/Xbox 1, etc
      // 2: 7th generation consoles, such as PS3/360, with sub-par CPUs.
      // 3: Modern desktop/laptops with reasonably powerful CPUs.
      // 4: High-end desktops with very powerful CPUs.
      unsigned int performanceLevel = *reinterpret_cast<const unsigned int*>(data);
      if (0 <= performanceLevel && performanceLevel <= 3)
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: performance hint: %d", cmd, performanceLevel);
      else if (performanceLevel == 4)
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: performance hint: I hope you have a badass computer...", cmd);
      else
        CLog::Log(LOGERROR, "GameClient environment query ID=%d: invalid performance hint: %d", cmd, performanceLevel);
      break;
    }
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    {
      // Returns a directory for storing system specific ROMs such as BIOSes, configuration data,
      // etc. The returned value can be NULL, in which case it's up to the implementation to find
      // a suitable directory.
      const char **strData = reinterpret_cast<const char**>(data);
      *strData = NULL;
      m_systemDirectory.clear();

      if (m_activeClient)
      {
        // The game client's first encounter with GET_SYSTEM_DIRECTORY is
        // graffitied by this setting. From this point on it will appear in
        // the directories tab of Game Settings.
        m_activeClient->UpdateSetting("hassystemdirectory", "true");

        if (m_activeClient->GetSetting("systemdirectory").length())
        {
          m_systemDirectory = m_activeClient->GetSetting("systemdirectory");
          // Avoid passing the game client a nonexistent directory. Note, if the
          // user chooses "skip" this passes NULL but preserves the setting.
          if (!CDirectory::Exists(m_systemDirectory))
            m_systemDirectory.clear();
        }

        if (!m_systemDirectory.length())
        {
          CContextButtons choices;
          choices.Add(0, 15035); // Choose system directory
          choices.Add(1, 15036); // Skip and try game directory
          choices.Add(2, 222); // Cancel

          int btnid = CGUIDialogContextMenu::ShowAndGetChoice(choices);
          if (btnid == 0)
          {
            // Setup the shares, system files might be located with game files so start there
            VECSOURCES shares;
            VECSOURCES *pGameShares = g_settings.GetSourcesFromType("games");
            if (pGameShares)
              shares = *pGameShares;

            // Always append local drives
            g_mediaManager.GetLocalDrives(shares);

            // "Choose system directory"
            if (CGUIDialogFileBrowser::ShowAndGetDirectory(shares, g_localizeStrings.Get(15035), m_systemDirectory))
              m_activeClient->UpdateSetting("systemdirectory", m_systemDirectory);
            else
              m_bAbort = true;
          }
          else if (btnid == 1)
          {
            // Proceed normally, passing NULL as data argument
          }
          else
          {
            m_bAbort = true;
          }
        }
        m_activeClient->SaveSettings();
      }

      if (m_systemDirectory.length())
        *strData = m_systemDirectory.c_str();

      if (*strData)
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: using system directory %s", cmd, m_systemDirectory.c_str());
      else
        CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: no system directory passed to game client", cmd);
      break;
    }
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
      // Get the internal pixel format used by the core. The default pixel format is
      // RETRO_PIXEL_FORMAT_0RGB1555 (see libretro.h). Returning false lets the core
      // know that XBMC does not support the pixel format.
      retro_pixel_format pix_fmt = *reinterpret_cast<const retro_pixel_format*>(data);
      // Validate the format
      switch (pix_fmt)
      {
      case RETRO_PIXEL_FORMAT_0RGB1555: // 5 bit color, high bit must be zero
      case RETRO_PIXEL_FORMAT_XRGB8888: // 8 bit color, high byte is ignored
      case RETRO_PIXEL_FORMAT_RGB565:   // 5/6/5 bit color
        {
          static const char *fmts[] = {"RETRO_PIXEL_FORMAT_0RGB1555",
                                       "RETRO_PIXEL_FORMAT_XRGB8888",
                                       "RETRO_PIXEL_FORMAT_RGB565"};
          CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: set pixel format: %d, %s", cmd, pix_fmt, fmts[pix_fmt]);
          if (fn_SetPixelFormat)
            fn_SetPixelFormat(pix_fmt);
          break;
        }
      default:
        CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: invalid pixel format: %d", cmd, pix_fmt);
        return false;
      }
      break;
    }
  case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    {
      // Describes the internal input bind through a human-readable string.
      // This string can be used to better let a user configure input. The array
      // is terminated by retro_input_descriptor::description being set to NULL.
      const retro_input_descriptor *descriptor = reinterpret_cast<const retro_input_descriptor*>(data);

      if (!descriptor->description)
        CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: no descriptors given", cmd);
      else
      {
        while (descriptor && descriptor->description)
        {
          CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: notified of input %s (port=%d, device=%d, index=%d, id=%d)",
            cmd, descriptor->description, descriptor->port, descriptor->device, descriptor->index, descriptor->id);
          descriptor++;
        }
      }
      break;
    }
  case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
    {
      // Sets a callback function, called by XBMC, used to notify core about
      // keyboard events.
      const retro_keyboard_callback *callback_struct = reinterpret_cast<const retro_keyboard_callback*>(data);
      if (callback_struct->callback && fn_SetKeyboardCallback)
        fn_SetKeyboardCallback(callback_struct->callback);
      break;
    }
  case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    {
      // Sets an interface to eject and insert disk images. This is used for games which
      // consist of multiple images and must be manually swapped out by the user (e.g. PSX).
      const retro_disk_control_callback *disk_control_cb = reinterpret_cast<const retro_disk_control_callback*>(data);
      if (fn_SetDiskControlCallback)
        fn_SetDiskControlCallback(disk_control_cb);
      break;
    }
  case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
      // Sets an interface to let a libretro core render with hardware acceleration.
      // This call is currently very experimental
      const retro_hw_render_callback *hw_render_cb = reinterpret_cast<const retro_hw_render_callback*>(data);
      if (fn_SetRenderCallback)
        fn_SetRenderCallback(hw_render_cb);
      break;
    }
  case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      // Interface to acquire user-defined information from environment that
      // cannot feasibly be supported in a multi-system way. Mostly used for
      // obscure, specific features that the user can tap into when necessary.
      retro_variable *var = reinterpret_cast<retro_variable*>(data);
      if (var->key && var->value)
      {
        // m_varMap provides both a static layer for returning persistent strings,
        // and a way of detecting stale data for RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE
        if (false && m_activeClient)
          m_varMap[var->key] = m_activeClient->GetSetting(var->key);
        else
          m_varMap[var->key] = "";

        if (false && m_varMap[var->key].length())
        {
          var->value = m_varMap[var->key].c_str();
          CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: variable %s set to %s", cmd, var->key, var->value);
        }
        else
        {
          var->value = NULL;
          CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: undefined variable %s", cmd, var->key);
        }
      }
      else
      {
        if (var->value)
          var->value = NULL;
        CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: no variable given", cmd);
      }
      break;
    }
  case RETRO_ENVIRONMENT_SET_VARIABLES:
    {
      // Allows an implementation to define its configuration options. 'data'
      // points to an array of retro_variable structs terminated by a
      // { NULL, NULL } element.
      const retro_variable *vars = reinterpret_cast<const retro_variable*>(data);
      if (!vars->key)
        CLog::Log(LOGERROR, "CLibretroEnvironment query ID=%d: no variables given", cmd);
      else
      {
        vector<TiXmlElement> xmlSettings;
        m_varMap.clear();

        while (vars && vars->key)
        {
          if (vars->value)
          {
            CLog::Log(LOGINFO, "CLibretroEnvironment query ID=%d: notified of var %s (%s)", cmd, vars->key, vars->value);
            TiXmlElement setting("setting");
            if (ParseVariable(*vars, setting, m_varMap[vars->key])) // m_varMap[vars->key] is always created
              xmlSettings.push_back(setting);
            else
              CLog::Log(LOGWARNING, "CLibretroEnvironment query ID=%d: error parsing variable");
          }
          else
            CLog::Log(LOGWARNING, "CLibretroEnvironment query ID=%d: var %s has no value", cmd, vars->key);
          vars++;
        }

        if (m_activeClient)
          m_activeClient->SetVariables(xmlSettings);
      }
      break;
    }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    {
      // Indicate that environment variables are stale and should be re-queried with GET_VARIABLE.
      bool stale = false;
      if (m_activeClient)
      {
        for (map<CStdString, CStdString>::const_iterator it = m_varMap.begin(); !stale && it != m_varMap.end(); it++)
          if (it->second.empty() || it->second != m_activeClient->GetSetting(it->first))
            stale = true;
      }
      *reinterpret_cast<bool*>(data) = stale;
      break;
    }
  }
  return true;
}

bool CLibretroEnvironment::ParseVariable(const retro_variable &var, TiXmlElement &xmlSetting, CStdString &strDefault)
{
  // Variable parsing follows a very procedural approach heavily grounded in C-think:
  // the value contains a description, a delimiting semicolon, a pipe-separated
  // list of values, and cries at the sight of object-oriented programming.
  // Example setting: "Speed hack coprocessor X; false|true"
  CStdString description;
  CStdString strValues(var.value);
  CStdStringArray values;
  unsigned short setting = SETTING_TYPE_NONE;

  // Boolean labels. For canonicalization purposes, make sure even strings are
  // synonymous with true and odd strings with false; NULL is allowed anywhere
  // if the count becomes uneven.
  const char *boolsetting[] = {"true", "false", "yes", "no"};

  stringstream strRegBool;
  strRegBool << "^(" << boolsetting[0];
  for (size_t i = 1; i < sizeof(boolsetting) / sizeof(boolsetting[0]); i++)
    if (boolsetting[i])
      strRegBool << '|' << boolsetting[i];
  strRegBool << ")$";

  CRegExp regBool, regIpAddress, regInt, regFloat, regPercent;
  if (!regBool.RegComp(strRegBool.str()) ||
      !regIpAddress.RegComp("^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$") || // Match to #.#.#.#
      !regInt.RegComp("^[0-9]+$") ||
      !regFloat.RegComp("^[0-9]*\\.[0-9]+$") ||
      !regPercent.RegComp("^[0-9]+%$"))
    return false;

  size_t pos;
  if ((pos = strValues.find(';')) != string::npos)
  {
    description = strValues.substr(0, pos);
    description = description.Trim();
    strValues = strValues.substr(pos + 1);
  }

  if (description.empty())
    description = var.key;

  StringUtils::SplitString(strValues, "|", values);

  // Infer setting type based on number and format of values
  if (!values.size())
    return false;
  if (values.size() == 1)
    setting |= (SETTING_TYPE_TEXT | SETTING_TYPE_IPADDRESS | SETTING_TYPE_NUMBER);
  if (values.size() == 2)
    setting |= SETTING_TYPE_BOOL;
  if (values.size() > 1)
    setting |= (SETTING_TYPE_SLIDER_INT | SETTING_TYPE_SLIDER_FLOAT | SETTING_TYPE_SLIDER_PERCENT | SETTING_TYPE_ENUM);

  for (CStdStringArray::iterator it = values.begin(); it != values.end(); it++)
  {
    (*it) = it->Trim();

    // Discard invalid settings types
    struct FilterTypes
    {
      CRegExp *regex;
      unsigned short setting;
    };
    FilterTypes filters[] =
    {
      { &regBool,      SETTING_TYPE_BOOL },
      { &regIpAddress, SETTING_TYPE_IPADDRESS },
      { &regInt,       SETTING_TYPE_NUMBER },
      { &regInt,       SETTING_TYPE_SLIDER_INT },
      { &regFloat,     SETTING_TYPE_SLIDER_FLOAT },
      { &regPercent,   SETTING_TYPE_SLIDER_PERCENT },
    };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++)
      if ((setting & filters[i].setting) && filters[i].regex->RegFind(*it) < 0)
        setting &= ~filters[i].setting;
  }

  // Got our type, construct final XML element. If two types are superimposed,
  // i.e. SETTING_TYPE_BOOL | SETTING_TYPE_ENUM, choose the more specific one.
  xmlSetting.SetAttribute("id", var.key);
  xmlSetting.SetAttribute("label", description.c_str());

  if (setting & SETTING_TYPE_BOOL)
  {
    // Canonicalize the default value
    for (size_t i = 0; i < sizeof(boolsetting) / sizeof(boolsetting[0]); i++)
      if (boolsetting[i] && values[0].Equals(boolsetting[i]))
        { values[0] = (i % 2 == 0 ? "true" : "false"); break; }
    xmlSetting.SetAttribute("type", "bool");
  }
  else if (setting & SETTING_TYPE_TEXT)
    xmlSetting.SetAttribute("type", "text");
  else if (setting & SETTING_TYPE_IPADDRESS)
    xmlSetting.SetAttribute("type", "ipaddress");
  else if (setting & SETTING_TYPE_NUMBER)
    xmlSetting.SetAttribute("type", "number");
  else if ((setting & SETTING_TYPE_SLIDER_INT) || (setting & SETTING_TYPE_SLIDER_PERCENT))
  {
    bool percent = (setting & SETTING_TYPE_SLIDER_PERCENT) != 0;
    vector<long int> intValues;
    for (CStdStringArray::const_iterator it = values.begin(); it != values.end(); it++)
      intValues.push_back(strtol(it->c_str(), NULL, 10));
    
    // TODO: Euclid's GCD
    int min = 0;
    int step = 5;
    int max = 100;
    int count = 21;

    // Only allow linear steps
    bool valid = (min + step * (count - 1) == max);
    if (valid && false)
    {
      CStdString range;
      range.Format("%d,%d,%d", min, step, max);
      xmlSetting.SetAttribute("type", "slider");
      xmlSetting.SetAttribute("option", percent ? "percent" : "int");
      xmlSetting.SetAttribute("range", range.c_str());
    }
    else
    {
      // Fall back to a simple enum
      StringUtils::JoinString(values, "|", strValues);
      xmlSetting.SetAttribute("type", "enum");
      xmlSetting.SetAttribute("values", strValues.c_str());
    }
  }
  else if ((setting & SETTING_TYPE_ENUM) || (setting & SETTING_TYPE_SLIDER_FLOAT))
  {
    StringUtils::JoinString(values, "|", strValues);
    xmlSetting.SetAttribute("type", "enum");
    xmlSetting.SetAttribute("values", strValues.c_str());
  }
  xmlSetting.SetAttribute("default", values[0].c_str());
  strDefault = values[0].c_str();

  return true;
}
