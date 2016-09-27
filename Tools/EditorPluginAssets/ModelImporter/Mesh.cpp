#include <PCH.h>
#include <EditorPluginAssets/ModelImporter/Mesh.h>
#include <Foundation/Logging/Log.h>

#include <ThirdParty/mikktspace/mikktspace.h>

namespace ezModelImporter
{
  VertexDataStream::VertexDataStream(ezUInt32 uiNumElementsPerVertex, ezUInt32 uiNumTriangles)
    : m_uiNumElementsPerVertex(uiNumElementsPerVertex)
  {
    m_IndexToData.SetCount(uiNumTriangles * 3);
  }

  void VertexDataStream::ReserveData(ezUInt32 numExpectedValues)
  {
    // +1 for the zero entry at the start of the array.
    m_Data.Reserve((numExpectedValues + 1) * m_uiNumElementsPerVertex);
  }

  Mesh::Mesh()
    : HierarchyObject(ObjectHandle::MESH)
    , m_uiNextUnusedVertexIndex(0)
  {}

  Mesh::~Mesh()
  {
    for (auto it = m_VertexDataStreams.GetIterator(); it.IsValid(); ++it)
    {
      EZ_DEFAULT_DELETE(it.Value());
    }
  }

  VertexDataStream* Mesh::AddDataStream(ezGALVertexAttributeSemantic::Enum semantic, ezUInt32 uiNumElementsPerVertex)
  {
    // A few checks for meaningful element count.
    switch (semantic)
    {
    case ezGALVertexAttributeSemantic::Position:
      EZ_ASSERT_DEBUG(uiNumElementsPerVertex == 3, "Position vertex streams should always have exactly 3 elements.");
      break;
    case ezGALVertexAttributeSemantic::Normal:
      EZ_ASSERT_DEBUG(uiNumElementsPerVertex == 3, "Normal vertex streams should always have exactly 3 elements.");
      break;
    case ezGALVertexAttributeSemantic::Tangent:
      EZ_ASSERT_DEBUG(uiNumElementsPerVertex == 3, "Tangent vertex streams should always have exactly 3 elements.");
      break;
    case ezGALVertexAttributeSemantic::BiTangent:
      EZ_ASSERT_DEBUG(uiNumElementsPerVertex == 3 || uiNumElementsPerVertex == 1, "BiTangent vertex streams should have either 3 elements (vector) or 1 element (sign).");
      break;
    }

    VertexDataStream* existingStream = nullptr;
    if (!m_VertexDataStreams.TryGetValue(static_cast<ezUInt32>(semantic), existingStream))
    {
      m_VertexDataStreams.Insert(semantic, EZ_DEFAULT_NEW(VertexDataStream, uiNumElementsPerVertex, m_Triangles.GetCount()));
      return m_VertexDataStreams[semantic];
    }
    else
    {
      if (uiNumElementsPerVertex != existingStream->GetNumElementsPerVertex())
      {
        return nullptr;
      }
      return existingStream;
    }
  }

  void Mesh::RemoveDataStream(ezGALVertexAttributeSemantic::Enum semantic)
  {
    m_VertexDataStreams.Remove(semantic);
  }

  VertexDataStream* Mesh::GetDataStream(ezGALVertexAttributeSemantic::Enum semantic)
  {
    VertexDataStream* out = nullptr;
    m_VertexDataStreams.TryGetValue(semantic, out);
    return out;
  }

  const VertexDataStream* Mesh::GetDataStream(ezGALVertexAttributeSemantic::Enum semantic) const
  {
    VertexDataStream* out = nullptr;
    m_VertexDataStreams.TryGetValue(semantic, out);
    return out;
  }

  void Mesh::AddTriangles(ezUInt32 num)
  {
    m_Triangles.Reserve(m_Triangles.GetCount() + num);
    for (ezUInt32 i = 0; i < num; ++i)
    {
      Triangle& tri = m_Triangles.ExpandAndGetRef();
      tri.m_Vertices[0].m_Value = m_uiNextUnusedVertexIndex; ++m_uiNextUnusedVertexIndex;
      tri.m_Vertices[1].m_Value = m_uiNextUnusedVertexIndex; ++m_uiNextUnusedVertexIndex;
      tri.m_Vertices[2].m_Value = m_uiNextUnusedVertexIndex; ++m_uiNextUnusedVertexIndex;
    }

    for (auto streamIt = m_VertexDataStreams.GetIterator(); streamIt.IsValid(); ++streamIt)
    {
      streamIt.Value()->m_IndexToData.Reserve(m_Triangles.GetCount() * 3);
      for (ezUInt32 i = 0; i < num * 3; ++i)
        streamIt.Value()->m_IndexToData.PushBack(VertexDataIndex());
    }
  }

  ezUInt32 Mesh::GetNumSubMeshes() const
  {
    return m_SubMeshes.GetCount();
  }

  const SubMesh& Mesh::GetSubMesh(ezUInt32 idx) const
  {
    return m_SubMeshes[idx];
  }

  SubMesh& Mesh::GetSubMesh(ezUInt32 idx)
  {
    return m_SubMeshes[idx];
  }

  void Mesh::AddSubMesh(SubMesh& mesh)
  {
    return m_SubMeshes.PushBack(mesh);
  }

  void Mesh::ApplyTransform(const ezTransform& transform)
  {
    ezMat4 transformMat = transform.GetAsMat4();

    for (auto it = m_VertexDataStreams.GetIterator(); it.IsValid(); ++it)
    {
      VertexDataStream* sourceStream = it.Value();

      // Positions
      if (it.Key() == ezGALVertexAttributeSemantic::Position)
      {
        for (ezUInt32 i = 0; i < sourceStream->m_Data.GetCount(); i += 3)
        {
          ezVec3& pos = *reinterpret_cast<ezVec3*>(&sourceStream[i]);
          pos = transformMat.TransformPosition(pos);
        }
      }

      // Directions
      else if (it.Key() == ezGALVertexAttributeSemantic::Normal ||
        it.Key() == ezGALVertexAttributeSemantic::Tangent ||
        it.Key() == ezGALVertexAttributeSemantic::BiTangent)
      {
        for (ezUInt32 i = 0; i < sourceStream->m_Data.GetCount(); i += 3)
        {
          ezVec3& dir = *reinterpret_cast<ezVec3*>(&sourceStream[i]);
          dir = transformMat.TransformDirection(dir);
        }
      }
    }
  }

  void Mesh::AddData(const Mesh& mesh)
  {
    // Create new triangles.
    ezUInt32 oldTriangleCount = GetNumTriangles();
    AddTriangles(mesh.GetNumTriangles());

    ezArrayPtr<const Mesh::Triangle> sourceTriangles = mesh.GetTriangles();
    ezArrayPtr<const Mesh::Triangle> targetTriangles = GetTriangles().GetSubArray(oldTriangleCount);
    EZ_ASSERT_DEBUG(sourceTriangles.GetCount() == targetTriangles.GetCount(), "Something is wrong with triangle allocation!");

    for (auto it = mesh.m_VertexDataStreams.GetIterator(); it.IsValid(); ++it)
    {
      const VertexDataStream* sourceStream = it.Value();
      VertexDataStream* targetStream = AddDataStream(static_cast<ezGALVertexAttributeSemantic::Enum>(it.Key()), sourceStream->GetNumElementsPerVertex());
      if (!targetStream)
      {
        ezLog::SeriousWarning("Cannot merge mesh %s properly since it has a vertex data stream with semantic %i that uses %i elements instead of %i which is used by the merge target. Skipping this data stream.",
          mesh.m_Name.GetData(), it.Key(), sourceStream->GetNumElementsPerVertex(), targetStream->GetNumElementsPerVertex());
        continue;
      }

      // Copy data.
      ezUInt32 targetBaseDataIndex = targetStream->m_Data.GetCount();
      targetStream->m_Data.PushBackRange(sourceStream->m_Data);

      // Set mapping
      for (ezUInt32 tri = 0; tri < sourceTriangles.GetCount(); ++tri)
      {
        for (int v = 0; v < 3; ++v)
        {
          VertexDataIndex sourceDataIndex = sourceStream->GetDataIndex(sourceTriangles[tri].m_Vertices[v]);
          if (sourceDataIndex.IsValid())
            targetStream->SetDataIndex(targetTriangles[tri].m_Vertices[v], targetBaseDataIndex + sourceDataIndex.GetValue());
        }
      }
    }

    // Add submeshes.
    ezUInt32 oldSubMeshCount = m_SubMeshes.GetCount();
    m_SubMeshes.PushBackRange(mesh.m_SubMeshes);
    for (ezUInt32 i = oldSubMeshCount; i < m_SubMeshes.GetCount(); ++i)
    {
      m_SubMeshes[i].m_uiFirstTriangle += oldTriangleCount;
    }
  }

  void Mesh::MergeSubMeshesWithSameMaterials()
  {
    // Find out which material maps to which submesh.
    // To make the result deterministic and change the order of submeshes/materials as little as possible, we cannot rely on maps or hashmaps
    bool anyDuplicate = false;
    typedef ezHybridArray<SubMesh*, 4> SubMeshBundle;
    ezDynamicArray<SubMeshBundle> subMeshBundles;
    for (int i = m_SubMeshes.GetCount() - 1; i >= 0; --i)
    {
      auto materialHandle = m_SubMeshes[i].m_Material;
      SubMeshBundle* existingBundle = nullptr;
      for (SubMeshBundle& bundle : subMeshBundles)
      {
        if (bundle[0]->m_Material == materialHandle)
        {
          existingBundle = &bundle;
          break;
        }
      }

      if (!existingBundle)
      {
        SubMeshBundle bundle;
        bundle.PushBack(&m_SubMeshes[i]);
        subMeshBundles.PushBack(bundle);
      }
      else
      {
        existingBundle->PushBack(&m_SubMeshes[i]);
        anyDuplicate = true;
      }
    }

    // Nothing to do?
    if (!anyDuplicate)
      return;

    // Rewrite triangle and submesh list.
    // Note that in the special case in which all triangles are already sorted correctly we perform really badly. Optimizing this however is a lot more code.
    ezDynamicArray<Triangle> trianglesNew;
    trianglesNew.Reserve(m_Triangles.GetCount());
    ezDynamicArray<SubMesh> subMeshesNew;
    subMeshesNew.Reserve(subMeshBundles.GetCount());
    for (SubMeshBundle& bundle : subMeshBundles)
    {
      SubMesh& newSubMesh = subMeshesNew.ExpandAndGetRef();
      newSubMesh.m_Material = bundle[0]->m_Material;
      newSubMesh.m_uiFirstTriangle = trianglesNew.GetCount();

      for (SubMesh* oldSubMesh : bundle)
      {
        newSubMesh.m_uiTriangleCount += oldSubMesh->m_uiTriangleCount;
        trianglesNew.PushBackRange(m_Triangles.GetArrayPtr().GetSubArray(oldSubMesh->m_uiFirstTriangle, oldSubMesh->m_uiTriangleCount));
      }
    }

    if (m_Triangles.GetCount() > trianglesNew.GetCount())
      ezLog::Warning("There were some triangles in submeshes of the mesh '%s' that were not referenced by any submesh. These triangles were discarded while merging submeshes.", m_Name.GetData());
    else if (m_Triangles.GetCount() < trianglesNew.GetCount())
      ezLog::Warning("There are submeshes in '%s' with overlapping triangle use. These triangles were duplicated while merging submeshes.", m_Name.GetData());

    m_Triangles = std::move(trianglesNew);
    m_SubMeshes = std::move(subMeshesNew);
  }

  ezResult Mesh::ComputeNormals()
  {
    VertexDataStream* positionStream = GetDataStream(ezGALVertexAttributeSemantic::Position);
    if (positionStream == nullptr)
    {
      ezLog::Error("Can't compute vertex normals for the mesh %s, because it doesn't have vertex positions.", m_Name.GetData());
      return EZ_FAILURE;
    }
    VertexDataStream* normalStream = AddDataStream(ezGALVertexAttributeSemantic::Normal, 3);

    // Normals have same mapping as positions.
    normalStream->m_IndexToData = positionStream->m_IndexToData;
    // Reset all normals to zero.
    normalStream->m_Data.SetCount(positionStream->m_Data.GetCount());
    ezMemoryUtils::ZeroFill<float>(normalStream->m_Data.GetData(), normalStream->m_Data.GetCount());

    // Compute unnormalized triangle normals and add them to all vertices.
    // This way large triangles have an higher influence on the vertex normal.
    for (const Triangle& triangle : m_Triangles)
    {
      ezVec3 p0 = positionStream->GetValueVec3(triangle.m_Vertices[0]);
      ezVec3 p1 = positionStream->GetValueVec3(triangle.m_Vertices[1]);
      ezVec3 p2 = positionStream->GetValueVec3(triangle.m_Vertices[2]);

      ezVec3 d01 = p1 - p0;
      ezVec3 d02 = p2 - p0;

      ezVec3 triNormal = d01.Cross(d02);
      normalStream->SetValue(triangle.m_Vertices[0], normalStream->GetValueVec3(triangle.m_Vertices[0]) + triNormal); // (possible optimization: have a special addValue to avoid unnecessary lookup)
      normalStream->SetValue(triangle.m_Vertices[1], normalStream->GetValueVec3(triangle.m_Vertices[1]) + triNormal);
      normalStream->SetValue(triangle.m_Vertices[2], normalStream->GetValueVec3(triangle.m_Vertices[2]) + triNormal);
    }

    // Normalize normals.
    for (ezUInt32 n = 0; n < normalStream->m_Data.GetCount(); n += 3)
      reinterpret_cast<ezVec3*>(&normalStream->m_Data[n])->NormalizeIfNotZero();

    return EZ_SUCCESS;
  }

  template <>
  struct ezHashHelper<ezVec3>
  {
    EZ_FORCE_INLINE static ezUInt32 Hash(const ezVec3& value)
    {
      // could do something more clever that uses the fact it is a normalized vector
      return ezHashing::MurmurHash(&value, sizeof(ezVec3));
    }

    EZ_FORCE_INLINE static bool Equal(const ezVec3& a, const ezVec3& b)
    {
      return a == b;
    }
  };

  ezResult Mesh::ComputeTangents()
  {
    struct MikkInterfaceImpl
    {
      ezArrayPtr<Triangle> triangles;
      const VertexDataStream* positionStream;
      const VertexDataStream* normalStream;
      const VertexDataStream* texStream;
      VertexDataStream* tangentStream;
      VertexDataStream* bitangentStream;
      VertexDataIndex bitangentIndexPositive;
      VertexDataIndex bitangentIndexNegative;

      ezHashTable<ezVec3, VertexDataIndex> tangentDataMap;

      int GetNumFaces() const { return triangles.GetCount(); }
      int GetNumVerticesOfFace(const int iFace) const { return 3; }

      void GetPosition(float fvPosOut[], const int iFace, const int iVert) const
      {
        ezVec3 p = positionStream->GetValueVec3(triangles[iFace].m_Vertices[iVert]);
        fvPosOut[0] = p.x;
        fvPosOut[1] = p.y;
        fvPosOut[2] = p.z;
      }
      void GetNormal(float fvNormOut[], const int iFace, const int iVert) const
      {
        ezVec3 n = normalStream->GetValueVec3(triangles[iFace].m_Vertices[iVert]);
        fvNormOut[0] = n.x;
        fvNormOut[1] = n.y;
        fvNormOut[2] = n.z;
      }
      void GetTexCoord(float fvTexcOut[], const int iFace, const int iVert) const
      {
        ezVec2 uv = texStream->GetValueVec2(triangles[iFace].m_Vertices[iVert]);
        fvTexcOut[0] = uv.x;
        fvTexcOut[1] = uv.y;
      }

      void SetTSpaceBasic(const float fvTangent[], const float fSign, const int iFace, const int iVert)
      {
        // Need to reconstruct indexing using hashmap lookups.
        // (will get a call to SetTSpaceBasic for every triangle vertex, not for every data vertex.)
        VertexIndex v = triangles[iFace].m_Vertices[iVert];
        ezVec3 key = ezVec3(fvTangent[0], fvTangent[1], fvTangent[2]);
        VertexDataIndex existingTangentIndex;
        if (tangentDataMap.TryGetValue(key, existingTangentIndex))
        {
          tangentStream->SetDataIndex(v, existingTangentIndex);
        }
        else
        {
          tangentStream->SetValue(v, ezVec3(fvTangent[0], fvTangent[1], fvTangent[2]));
          tangentDataMap.Insert(key, tangentStream->GetDataIndex(v));
        }

        // For bitangent sign its easy: There are only 2 signs and we've set the data already.
        if(fSign >= 1.0f)
          bitangentStream->SetDataIndex(v, bitangentIndexPositive);
        else
          bitangentStream->SetDataIndex(v, bitangentIndexNegative);
      }
    } mikkInterface;
    mikkInterface.triangles = m_Triangles;

    mikkInterface.positionStream = GetDataStream(ezGALVertexAttributeSemantic::Position);
    if (mikkInterface.positionStream == nullptr)
    {
      ezLog::Error("Can't compute vertex tangents for the mesh %s, because it doesn't have vertex positions.", m_Name.GetData());
      return EZ_FAILURE;
    }
    mikkInterface.normalStream = GetDataStream(ezGALVertexAttributeSemantic::Normal);
    if (mikkInterface.normalStream == nullptr)
    {
      ezLog::Error("Can't compute tangents for the mesh %s, because it doesn't have vertex noramls.", m_Name.GetData());
      return EZ_FAILURE;
    }
    mikkInterface.texStream = GetDataStream(ezGALVertexAttributeSemantic::TexCoord0);
    if (mikkInterface.texStream == nullptr || mikkInterface.texStream->GetNumElementsPerVertex() != 2)
    {
      ezLog::Error("Can't compute tangents for the mesh %s, because it doesn't have TexCoord0 stream with two components.", m_Name.GetData());
      return EZ_FAILURE;
    }

    // Make sure tangent stream exists.
    mikkInterface.tangentStream = AddDataStream(ezGALVertexAttributeSemantic::Tangent, 3);
    
    // If there is already a data stream with 3 component bitangents, remove it.
    mikkInterface.bitangentStream = GetDataStream(ezGALVertexAttributeSemantic::BiTangent);
    if (mikkInterface.bitangentStream && mikkInterface.bitangentStream->GetNumElementsPerVertex() != 1)
      RemoveDataStream(ezGALVertexAttributeSemantic::BiTangent);
    mikkInterface.bitangentStream = AddDataStream(ezGALVertexAttributeSemantic::BiTangent, 1);
    float biTangentSignValues[] = { -1.0f, 1.0f };
    mikkInterface.bitangentStream->AddValues(ezMakeArrayPtr(biTangentSignValues));
    mikkInterface.bitangentIndexNegative = VertexDataIndex(0);
    mikkInterface.bitangentIndexPositive = VertexDataIndex(1);

    // Use Morton S. Mikkelsen's tangent calculation.
    SMikkTSpaceContext context;
    SMikkTSpaceInterface functions;
    context.m_pUserData = &mikkInterface;
    context.m_pInterface = &functions;
    functions.m_getNumFaces = [](const SMikkTSpaceContext* pContext) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->GetNumFaces(); };
    functions.m_getNumVerticesOfFace = [](const SMikkTSpaceContext* pContext, const int iFace) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->GetNumVerticesOfFace(iFace); };
    functions.m_getPosition = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->GetPosition(fvPosOut, iFace, iVert); };
    functions.m_getNormal = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->GetNormal(fvPosOut, iFace, iVert); };
    functions.m_getTexCoord = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->GetTexCoord(fvPosOut, iFace, iVert); };
    functions.m_setTSpaceBasic = [](const SMikkTSpaceContext * pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) { return static_cast<MikkInterfaceImpl*>(pContext->m_pUserData)->SetTSpaceBasic(fvTangent, fSign, iFace, iVert); };
    functions.m_setTSpace = nullptr;

    if (!genTangSpaceDefault(&context))
    {
      ezLog::Error("Failed to compute compute tangents for the mesh %s.", m_Name.GetData());
      return EZ_FAILURE;
    }

    return EZ_SUCCESS;
  }
}