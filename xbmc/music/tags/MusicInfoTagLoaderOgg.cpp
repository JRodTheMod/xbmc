/*
 *      Copyright (C) 2005-2012 Team XBMC
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

#include "MusicInfoTagLoaderOgg.h"
#include "OggTag.h"
#include "utils/log.h"

using namespace MUSIC_INFO;

CMusicInfoTagLoaderOgg::CMusicInfoTagLoaderOgg(void)
{}

CMusicInfoTagLoaderOgg::~CMusicInfoTagLoaderOgg()
{}

bool CMusicInfoTagLoaderOgg::Load(const CStdString& strFileName, CMusicInfoTag& tag, EmbeddedArt *art)
{
  try
  {
    // retrieve the OGG Tag info from strFileName
    // and put it in tag
    COggTag myTag;
    myTag.SetArt(art);
    if (myTag.Read(strFileName))
    {
      myTag.GetMusicInfoTag(tag);
    }
    return tag.Loaded();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Tag loader ogg: exception in file %s", strFileName.c_str());
  }

  tag.SetLoaded(false);
  return false;
}