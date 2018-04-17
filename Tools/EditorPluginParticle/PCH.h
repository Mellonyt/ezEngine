#pragma once

#include <Foundation/Basics.h>

// <StaticLinkUtil::StartHere>
// all include's before this will be left alone and not replaced by the StaticLinkUtil
// all include's AFTER this will be removed by the StaticLinkUtil and updated by what is actually used throughout the library

#include <Plugin.h>


#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/TranslationLookup.h>

#include <ToolsFoundation/Reflection/ToolsReflectionUtils.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <EditorFramework/Assets/AssetCurator.h>
