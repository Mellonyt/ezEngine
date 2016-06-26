#include <PCH.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/Algorithm/Hashing.h>
#include <Foundation/IO/MemoryStream.h>
#include <EditorFramework/Assets/AssetDocumentManager.h>

ezUpdateTask::ezUpdateTask()
{
}

void ezUpdateTask::Execute()
{
  ezUuid assetGuid;
  if (!ezAssetCurator::GetSingleton()->GetNextAssetToUpdate(assetGuid, m_sAssetPath))
    return;

  const ezDocumentTypeDescriptor* pTypeDescriptor = nullptr;
  if (ezDocumentManager::FindDocumentTypeFromPath(m_sAssetPath, false, pTypeDescriptor).Failed())
    return;

  ezUInt64 uiAssetHash = 0;
  ezAssetCurator::GetSingleton()->IsAssetUpToDate(assetGuid, nullptr, pTypeDescriptor, uiAssetHash);
}

ezUInt64 ezAssetCurator::HashFile(ezStreamReader& InputStream, ezStreamWriter* pPassThroughStream)
{
  ezUInt8 uiCache[1024 * 10];
  ezUInt64 uiHash = 0;

  while (true)
  {
    ezUInt64 uiRead = InputStream.ReadBytes(uiCache, EZ_ARRAY_SIZE(uiCache));

    if (uiRead == 0)
      break;

    uiHash = ezHashing::MurmurHash64(uiCache, (size_t)uiRead, uiHash);

    if (pPassThroughStream != nullptr)
      pPassThroughStream->WriteBytes(uiCache, uiRead);
  }

  return uiHash;
}

bool ezAssetCurator::GetNextAssetToUpdate(ezUuid& guid, ezStringBuilder& out_sAbsPath)
{
  ezLock<ezMutex> ml(m_CuratorMutex);

  while (!m_TransformStateUnknown.IsEmpty())
  {
    auto it = m_TransformStateUnknown.GetIterator();
    guid = it.Key();

    m_TransformStateUnknown.Remove(it);

    auto pAssetInfo = GetAssetInfo(guid);
    if (pAssetInfo == nullptr)
      continue;

    out_sAbsPath = GetAssetInfo(guid)->m_sAbsolutePath;
    return true;
  }

  return false;
}

void ezAssetCurator::OnUpdateTaskFinished(ezTask* pTask)
{
  ezLock<ezMutex> ml(m_CuratorMutex);

  RunNextUpdateTask();
}

void ezAssetCurator::RunNextUpdateTask()
{
  ezLock<ezMutex> ml(m_CuratorMutex);

  if (!m_bRunUpdateTask || m_TransformStateUnknown.IsEmpty())
    return;

  if (m_pUpdateTask == nullptr)
  {
    m_pUpdateTask = EZ_DEFAULT_NEW(ezUpdateTask);
    m_pUpdateTask->SetOnTaskFinished(ezMakeDelegate(&ezAssetCurator::OnUpdateTaskFinished, this));
  }

  if (m_pUpdateTask->IsTaskFinished())
    ezTaskSystem::StartSingleTask(m_pUpdateTask, ezTaskPriority::FileAccess);
}

ezUInt64 ezAssetCurator::GetAssetDependencyHash(ezUuid assetGuid)
{
  return GetAssetHash(assetGuid, false);
}

ezUInt64 ezAssetCurator::GetAssetReferenceHash(ezUuid assetGuid)
{
  return GetAssetHash(assetGuid, true);
}

ezUInt64 ezAssetCurator::GetAssetHash(ezUuid assetGuid, bool bReferences)
{
  if (EnsureAssetInfoUpdated(assetGuid).Failed())
  {
    ezLog::Error("Asset with GUID %s is unknown", ezConversionUtils::ToString(assetGuid).GetData());
    return 0;
  }

  EZ_LOCK(m_CuratorMutex);

  ezAssetInfo* pInfo = nullptr;
  if (!m_KnownAssets.TryGetValue(assetGuid, pInfo))
  {
    ezLog::Error("Asset with GUID %s is unknown", ezConversionUtils::ToString(assetGuid).GetData());
    return 0;
  }

  EZ_LOG_BLOCK("ezAssetCurator::GetAssetHash", pInfo->m_sAbsolutePath.GetData());

  ezFileStats stat;

  if (ezOSFile::GetFileStats(pInfo->m_sAbsolutePath, stat).Failed())
  {
    ezLog::Error("Failed to retrieve file stats '%s'", pInfo->m_sAbsolutePath.GetData());
    return 0;
  }

  // hash of the main asset file
  ezUInt64 uiHashResult = pInfo->m_Info.m_uiSettingsHash;

  // Iterate dependencies
  ezSet<ezString>& files = bReferences ? pInfo->m_Info.m_FileReferences : pInfo->m_Info.m_FileDependencies;

  for (const auto& dep : files)
  {
    ezString sPath = dep;

    if (sPath.IsEmpty())
      continue;

    if (ezConversionUtils::IsStringUuid(sPath))
    {
      const ezUuid guid = ezConversionUtils::ConvertStringToUuid(sPath);
      uiHashResult += GetAssetHash(guid, bReferences);
      continue;
    }

    if (!ezQtEditorApp::GetSingleton()->MakeDataDirectoryRelativePathAbsolute(sPath))
    {
      if (sPath.EndsWith(".color"))
      {
        // TODO: detect non-file assets and skip already in dependency gather function.
        continue;
      }
      ezLog::Error("Failed to make path absolute '%s'", sPath.GetData());
      return 0;
    }

    if (ezOSFile::GetFileStats(sPath, stat).Failed())
    {
      ezLog::Error("Failed to retrieve file stats '%s'", sPath.GetData());
      return 0;
    }

    auto& fileref = m_ReferencedFiles[sPath];

    // if the file has been modified, make sure to get updated data
    if (!fileref.m_Timestamp.IsEqual(stat.m_LastModificationTime, ezTimestamp::CompareMode::Identical))
    {
      ezFileReader file;
      if (file.Open(sPath).Failed())
      {
        ezLog::Error("Failed to open file '%s'", sPath.GetData());
        return 0;
      }
      fileref.m_Timestamp = stat.m_LastModificationTime;
      fileref.m_uiHash = ezAssetCurator::HashFile(file, nullptr);
      fileref.m_Status = ezAssetCurator::FileStatus::Status::Valid;
    }

    // combine all hashes
    uiHashResult += fileref.m_uiHash;
  }

  if (pInfo->m_LastAssetDependencyHash != uiHashResult)
  {
    pInfo->m_LastAssetDependencyHash = uiHashResult;
  }
  return uiHashResult;
}

