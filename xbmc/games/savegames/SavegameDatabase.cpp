/*
 *      Copyright (C) 2012 Team XBMC
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

#include "SavegameDatabase.h"
#include "SavegameInfoTag.h"
#include "dbwrappers/dataset.h"
#include "dialogs/GUIDialogProgress.h"
#include "settings/AdvancedSettings.h"
#include "utils/log.h"
//#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
//#include "utils/Variant.h"
#include "FileItem.h"

#include <vector>
#include <set>
#include <queue>

using namespace std;

CSavegameDatabase::CSavegameDatabase() : CDynamicDatabase("savegame")
{
  BeginDeclarations();
  DeclareIndex("file", "VARCHAR(256)");
  DeclareOneToMany("path", "VARCHAR(512)");
  DeclareOneToMany("folder", "VARCHAR(64)");
  DeclareOneToMany("year", "INTEGER");
  DeclareOneToMany("camera", "VARCHAR(72)"); // EXIF model is 32 bytes, make is 40

  // Version 2
  DeclareManyToMany("tag", "VARCHAR(64)");
}

bool CSavegameDatabase::Open()
{
  return CDynamicDatabase::Open(g_advancedSettings.m_databaseSavegames);
}

bool CSavegameDatabase::CreateTables()
{
  try
  {
    BeginTransaction();
    if (!CDynamicDatabase::CreateTables())
      return false;

    // Add the hash column to the path table
    CStdString strSQL = PrepareSQL("ALTER TABLE path ADD hash TEXT");
    m_pDS->exec(strSQL.c_str());

    CommitTransaction();
    return true;
  }
  catch (dbiplus::DbErrors&)
  {
    CLog::Log(LOGERROR, "SavegameDatabase: unable to create tables (error %i)", (int)GetLastError());
    RollbackTransaction();
  }
  return false;
}

bool CSavegameDatabase::UpdateOldVersion(int version)
{
  if (version < 1)
  {
    BeginDeclarations();
    DeclareIndex("file", "VARCHAR(128)");
    DeclareOneToMany("path", "VARCHAR(512)");
    DeclareOneToMany("folder", "VARCHAR(64)");
    DeclareOneToMany("year", "INTEGER");
    DeclareOneToMany("camera", "VARCHAR(72)");

    // Add the hash column to the path table
    CStdString strSQL = PrepareSQL("ALTER TABLE path ADD hash TEXT");
    m_pDS->exec(strSQL.c_str());
  }
  if (version < 2)
  {
    BeginDeclarations();
    DeclareIndex("file", "VARCHAR(128)");
    DeclareOneToMany("path", "VARCHAR(512)");
    DeclareOneToMany("folder", "VARCHAR(64)");
    DeclareOneToMany("year", "INTEGER");
    DeclareOneToMany("camera", "VARCHAR(72)");

    AddManyToMany("tag", "VARCHAR(64)");
  }

  return true;
}

bool CSavegameDatabase::Exists(const bson *object, int &idObject)
{
  if (!IsValid(object))
    return false;
  CStdString strSQL = PrepareSQL(
    "SELECT picture.idpicture "
    "FROM picture JOIN path ON path.idpath=picture.idpath "
    "WHERE file='%s' AND path='%s'",
    GetField(object, "file").asString().c_str(), GetField(object, "path").asString().c_str()
  );
  if (m_pDS->query(strSQL.c_str()))
  {
    bool bFound = false;
    if (m_pDS->num_rows() != 0)
    {
      idObject = m_pDS->fv(0).get_asInt();
      bFound = true;
    }
    m_pDS->close();
    return bFound;
  }
  return false;
}
/*
bool CSavegameDatabase::IsValid(const bson *object) const
{
  CVariant file = GetField(object, "file");
  string strFile = file.asString();
  CVariant path = GetField(object, "path");
  string strPath = path.asString();

  return !strFile.empty() && !strPath.empty();
}
*/
bool CSavegameDatabase::IsValid(const bson *object) const
{
  return !GetField(object, "file").asString().empty() && !GetField(object, "path").asString().empty();
}

CFileItem* CSavegameDatabase::CreateFileItem(const string &strBson, int id) const
{
  CSavegameInfoTag p;
  DeserializeBSON(strBson, id, &p);
  CStdString title = p.GetFilename();
  URIUtils::RemoveExtension(title);

  CFileItem *item = new CFileItem(title);
  item->SetPath(URIUtils::AddFileToFolder(p.GetPath(), p.GetFilename()));
  *item->GetPictureInfoTag() = p;
  item->m_dwSize = p.GetFileSize();
  CDateTime datetime;
  if (p.GetDateTime(datetime))
    item->m_dateTime = datetime;
  item->m_bIsFolder = false;
  return item;
}

bool CSavegameDatabase::GetPaths(set<string> &paths)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  paths.clear();
  try
  {
    strSQL = PrepareSQL("SELECT path FROM path");

    if (m_pDS->query(strSQL.c_str()))
    {
      while (!m_pDS->eof())
      {
        paths.insert(m_pDS->fv("path").get_asString());
        m_pDS->next();
      }
      m_pDS->close();
    }
  }
  catch (dbiplus::DbErrors &e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  return paths.size() > 0;
}

bool CSavegameDatabase::GetPathHash(const string &strDirectory, string &dbHash)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  try
  {
    strSQL = PrepareSQL("SELECT hash FROM path WHERE path='%s'", strDirectory.c_str());

    if (m_pDS->query(strSQL.c_str()))
    {
      if (m_pDS->num_rows() != 0)
      {
        dbHash = m_pDS->fv("hash").get_asString();
        m_pDS->close();
        return true;
      }
      m_pDS->close();
    }
  }
  catch (dbiplus::DbErrors e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  return false;
}

bool CSavegameDatabase::SetPathHash(const string &strDirectory, const string &dbHash)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  try
  {
    strSQL = PrepareSQL("UPDATE path SET hash='%s' WHERE path='%s'", dbHash.c_str(), strDirectory.c_str());

    if (m_pDS->exec(strSQL.c_str()))
      return true;
  }
  catch (dbiplus::DbErrors &e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  return false;
}

bool CSavegameDatabase::HasPath(const string &strPath)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  try
  {
    strSQL = PrepareSQL("SELECT path FROM path WHERE path='%s'", strPath.c_str());

    if (m_pDS->query(strSQL.c_str()))
    {
      bool bFound = false;
      if (m_pDS->num_rows() != 0)
        bFound = true;
      m_pDS->close();
      return bFound;
    }
  }
  catch (dbiplus::DbErrors &e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  return false;
}

bool CSavegameDatabase::GetPicturesByPath(const string &path, vector<CPictureInfoTag> &pictures)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  try
  {
    strSQL = PrepareSQL(
      "SELECT picture.idpicture, strContentBSON64 "
      "FROM picture JOIN path ON picture.idpath=path.idpath "
      "WHERE path.path='%s'",
      path.c_str()
    );

    if (m_pDS->query(strSQL.c_str()))
    {
      pictures.clear();
      while (!m_pDS->eof())
      {
        CPictureInfoTag p;
        DeserializeBSON(m_pDS->fv(1).get_asString(), m_pDS->fv(0).get_asInt(), &p);
        pictures.push_back(p);
        m_pDS->next();
      }
      m_pDS->close();
      return true;
    }
  }
  catch (dbiplus::DbErrors &e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  return false;
}

bool CSavegameDatabase::DeletePicturesByPath(const string &path, bool recursive, CGUIDialogProgress* pDialogProgress)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  CStdString strSQL;
  try
  {
    strSQL = "SELECT idpicture "
             "FROM picture JOIN path ON path.idpath=picture.idpath ";
    if (recursive)
      strSQL += PrepareSQL("WHERE SUBSTR(path,1,%i)='%s'", StringUtils::utf8_strlen(path.c_str()), path.c_str());
    else
      strSQL += PrepareSQL("WHERE path='%s'", path.c_str());

    queue<int> ids;
    if (m_pDS->query(strSQL.c_str()))
    {
      while (!m_pDS->eof())
      {
        ids.push(m_pDS->fv(0).get_asInt());
        m_pDS->next();
      }
      m_pDS->close();
    }

    if (pDialogProgress)
    {
      // Show progress dialog if we have to connect to freedb.org
      pDialogProgress->SetHeading(1); // Pictures
      pDialogProgress->SetLine(0, 703); // Removing pictures from the library
      pDialogProgress->SetLine(1, "");
      pDialogProgress->SetLine(2, "");
      pDialogProgress->SetPercentage(0);
      pDialogProgress->ShowProgressBar(false);
      pDialogProgress->StartModal();
    }

    size_t total = ids.size();
    while (ids.size())
    {
      DeleteObject(ids.front());
      ids.pop();
      if (pDialogProgress)
      {
        pDialogProgress->SetPercentage(100 * (total - ids.size()) / total);
        pDialogProgress->Progress();
        if (pDialogProgress->IsCanceled())
        {
          pDialogProgress->Close();
          m_pDS->close();
          return false;
        }
      }
    }

    if (pDialogProgress)
      pDialogProgress->Close();

    return true;
  }
  catch (dbiplus::DbErrors &e)
  {
    CLog::Log(LOGERROR, "%s failed (%s). SQL: %s", __FUNCTION__, e.getMsg(), strSQL.c_str());
  }
  if (pDialogProgress && pDialogProgress->IsActive())
    pDialogProgress->Close();
  return false;
}
