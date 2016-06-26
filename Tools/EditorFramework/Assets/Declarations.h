#pragma once

class ezImage;
class ezAssetFileHeader;

struct ezAssetDocumentFlags
{
  typedef ezUInt8 StorageType;

  enum Enum
  {
    None = 0,
    AutoTransformOnSave = EZ_BIT(0),      ///< Every time the document is saved, TransformAsset is automatically executed
    DisableTransform = EZ_BIT(1),         ///< If set, TransformAsset will not do anything
    OnlyTransformManually = EZ_BIT(2),    ///< The asset transformation is not done, unless explicitly requested for this asset
    SupportsThumbnail = EZ_BIT(3),       ///< The asset supports thumbnail generation (InternalCreateThumbnail must be implemented). 
    AutoThumbnailOnTransform = EZ_BIT(4),///< Thumbnail is automatically generated on transform.
    Default = None
  };

  struct Bits
  {
    StorageType AutoTransformOnSave : 1;
    StorageType DisableTransform : 1;
    StorageType OnlyTransformManually : 1;
  };
};

EZ_DECLARE_FLAGS_OPERATORS(ezAssetDocumentFlags)
