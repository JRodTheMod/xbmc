/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "TestBasicEnvironment.h"
#include "TestUtils.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "powermanagement/PowerManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "Util.h"

#include <cstdio>
#include <cstdlib>
#include <climits>

void TestBasicEnvironment::SetUp()
{
  char *tmp;
  CStdString xbmcTempPath;
  CStdString xbmcTempProfilePath;
  XFILE::CFile *f;

  CSettings::Get().Initialize();

  /* NOTE: The below is done to fix memleak warning about unitialized variable
   * in xbmcutil::GlobalsSingleton<CAdvancedSettings>::getInstance().
   */
  g_advancedSettings.Initialize();

  if (!CXBMCTestUtils::Instance().SetReferenceFileBasePath())
    SetUpError();
  CXBMCTestUtils::Instance().setTestFileFactoryWriteInputFile(
    XBMC_REF_FILE_PATH("xbmc/filesystem/test/reffile.txt")
  );

//for darwin set framework path - else we get assert
//in guisettings init below
#ifdef TARGET_DARWIN
  CStdString frameworksPath = CUtil::GetFrameworksPath();
  CSpecialProtocol::SetXBMCFrameworksPath(frameworksPath);    
#endif
  /* TODO: Something should be done about all the asserts in GUISettings so
   * that the initialization of these components won't be needed.
   */
  g_powerManager.Initialize();
  g_guiSettings.Initialize();

  /* Create a temporary directory and set it to be used throughout the
   * test suite run.
   */
#ifdef TARGET_WINDOWS
  TCHAR lpTempPathBuffer[MAX_PATH];
  if (!GetTempPath(MAX_PATH, lpTempPathBuffer))
    SetUpError();
  xbmcTempPath = lpTempPathBuffer;
  if (!GetTempFileName(xbmcTempPath.c_str(), "xbmctempdir", 0, lpTempPathBuffer))
    SetUpError();
  DeleteFile(lpTempPathBuffer);
  if (!CreateDirectory(lpTempPathBuffer, NULL))
    SetUpError();
  CSpecialProtocol::SetTempPath(lpTempPathBuffer);
#else
  char buf[MAX_PATH];
  (void)xbmcTempPath;
  strcpy(buf, "/tmp/xbmctempdirXXXXXX");
  if ((tmp = mkdtemp(buf)) == NULL)
    SetUpError();
  CSpecialProtocol::SetTempPath(tmp);
#endif

  /* Create a temporary master profile directory.
   */
#ifndef _LINUX
  TCHAR lpTempProfilePathBuffer[MAX_PATH];
  (void)tmp;
  if (!GetTempPath(MAX_PATH, lpTempProfilePathBuffer))
    SetUpError();
  xbmcTempProfilePath = lpTempProfilePathBuffer;
  if (!GetTempFileName(xbmcTempProfilePath.c_str(), "xbmcprofiledir", 0, lpTempProfilePathBuffer))
    SetUpError();
  DeleteFile(lpTempProfilePathBuffer);
  if (!CreateDirectory(lpTempProfilePathBuffer, NULL))
    SetUpError();
  CSpecialProtocol::SetMasterProfilePath(lpTempProfilePathBuffer);
#else
  char buf2[MAX_PATH];
  (void)xbmcTempProfilePath;
  strcpy(buf2, "/tmp/xbmcprofiledirXXXXXX");
  if ((tmp = mkdtemp(buf2)) == NULL)
    SetUpError();
  CSpecialProtocol::SetMasterProfilePath(tmp);
#endif

  // To resolve special://masterprofile/
  CSettings::Get().LoadProfiles(PROFILES_FILE);

  /* Create and delete a tempfile to initialize the VFS (really to initialize
   * CLibcdio). This is done so that the initialization of the VFS does not
   * affect the performance results of the test cases.
   */
  /* TODO: Make the initialization of the VFS here optional so it can be
   * testable in a test case.
   */
  f = XBMC_CREATETEMPFILE("");
  if (!f || !XBMC_DELETETEMPFILE(f))
  {
    TearDown();
    SetUpError();
  }
}

void TestBasicEnvironment::TearDown()
{
  CStdString xbmcTempPath = CSpecialProtocol::TranslatePath("special://temp/");
  XFILE::CDirectory::Remove(xbmcTempPath);
  CStdString xbmcTempProfilePath = CSpecialProtocol::TranslatePath("special://masterprofile/");
  XFILE::CDirectory::Remove(xbmcTempProfilePath);
}

void TestBasicEnvironment::SetUpError()
{
  fprintf(stderr, "Setup of basic environment failed.\n");
  exit(EXIT_FAILURE);
}
