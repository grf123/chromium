// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/chrome_app_list_item.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service_factory.h"
#include "chrome/browser/ui/app_list/chrome_app_list_model_updater.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/extension_system.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace {

AppListControllerDelegate* g_controller_for_test = nullptr;

}  // namespace

// static
void ChromeAppListItem::OverrideAppListControllerDelegateForTesting(
    AppListControllerDelegate* controller) {
  g_controller_for_test = controller;
}

// static
gfx::ImageSkia ChromeAppListItem::CreateDisabledIcon(
    const gfx::ImageSkia& icon) {
  const color_utils::HSL shift = {-1, 0, 0.6};
  return gfx::ImageSkiaOperations::CreateHSLShiftedImage(icon, shift);
}

// ChromeAppListItem::TestApi
ChromeAppListItem::TestApi::TestApi(ChromeAppListItem* item) : item_(item) {}

void ChromeAppListItem::TestApi::SetFolderId(const std::string& folder_id) {
  item_->SetFolderId(folder_id);
}

void ChromeAppListItem::TestApi::SetPosition(
    const syncer::StringOrdinal& position) {
  item_->SetPosition(position);
}

// ChromeAppListItem
ChromeAppListItem::ChromeAppListItem(Profile* profile,
                                     const std::string& app_id)
    : metadata_(ash::mojom::AppListItemMetadata::New(app_id,
                                                     "",
                                                     "",
                                                     syncer::StringOrdinal(),
                                                     false)),
      profile_(profile) {}

ChromeAppListItem::~ChromeAppListItem() {
}

void ChromeAppListItem::SetIsInstalling(bool is_installing) {
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemIsInstalling(id(), is_installing);
}

void ChromeAppListItem::SetPercentDownloaded(int32_t percent_downloaded) {
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemPercentDownloaded(id(), percent_downloaded);
}

void ChromeAppListItem::SetMetadata(
    ash::mojom::AppListItemMetadataPtr metadata) {
  metadata_ = std::move(metadata);
}

ash::mojom::AppListItemMetadataPtr ChromeAppListItem::CloneMetadata() const {
  return metadata_.Clone();
}

void ChromeAppListItem::Activate(int event_flags) {}

const char* ChromeAppListItem::GetItemType() const {
  return "";
}

ui::MenuModel* ChromeAppListItem::GetContextMenuModel() {
  return nullptr;
}

bool ChromeAppListItem::IsBadged() const {
  return false;
}

app_list::AppContextMenu* ChromeAppListItem::GetAppContextMenu() {
  return nullptr;
}

void ChromeAppListItem::ContextMenuItemSelected(int command_id,
                                                int event_flags) {
  if (GetAppContextMenu())
    GetAppContextMenu()->ExecuteCommand(command_id, event_flags);
}

extensions::AppSorting* ChromeAppListItem::GetAppSorting() {
  return extensions::ExtensionSystem::Get(profile())->app_sorting();
}

AppListControllerDelegate* ChromeAppListItem::GetController() {
  return g_controller_for_test != nullptr
             ? g_controller_for_test
             : AppListService::Get()->GetControllerDelegate();
}

void ChromeAppListItem::UpdateFromSync(
    const app_list::AppListSyncableService::SyncItem* sync_item) {
  DCHECK(sync_item && sync_item->item_ordinal.IsValid());
  // An existing synced position exists, use that.
  SetPosition(sync_item->item_ordinal);
  // Only set the name from the sync item if it is empty.
  if (name().empty())
    SetName(sync_item->item_name);
}

void ChromeAppListItem::SetDefaultPositionIfApplicable() {
  syncer::StringOrdinal page_ordinal;
  syncer::StringOrdinal launch_ordinal;
  extensions::AppSorting* app_sorting = GetAppSorting();
  if (!app_sorting->GetDefaultOrdinals(id(), &page_ordinal,
                                       &launch_ordinal) ||
      !page_ordinal.IsValid() || !launch_ordinal.IsValid()) {
    app_sorting->EnsureValidOrdinals(id(), syncer::StringOrdinal());
    page_ordinal = app_sorting->GetPageOrdinal(id());
    launch_ordinal = app_sorting->GetAppLaunchOrdinal(id());
  }
  DCHECK(page_ordinal.IsValid());
  DCHECK(launch_ordinal.IsValid());
  SetPosition(syncer::StringOrdinal(page_ordinal.ToInternalValue() +
                                    launch_ordinal.ToInternalValue()));
}

void ChromeAppListItem::SetIcon(const gfx::ImageSkia& icon) {
  icon_ = icon;
  icon_.EnsureRepsForSupportedScales();
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemIcon(id(), icon);
}

void ChromeAppListItem::SetName(const std::string& name) {
  metadata_->name = name;
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemName(id(), name);
}

void ChromeAppListItem::SetNameAndShortName(const std::string& name,
                                            const std::string& short_name) {
  metadata_->name = name;
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemNameAndShortName(id(), name, short_name);
}

void ChromeAppListItem::SetFolderId(const std::string& folder_id) {
  metadata_->folder_id = folder_id;
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemFolderId(id(), folder_id);
}

void ChromeAppListItem::SetPosition(const syncer::StringOrdinal& position) {
  metadata_->position = position;
  AppListModelUpdater* updater = model_updater();
  if (updater)
    updater->SetItemPosition(id(), position);
}

bool ChromeAppListItem::CompareForTest(const ChromeAppListItem* other) const {
  return id() == other->id() && folder_id() == other->folder_id() &&
         name() == other->name() && GetItemType() == other->GetItemType() &&
         position().Equals(other->position());
}

std::string ChromeAppListItem::ToDebugString() const {
  return id().substr(0, 8) + " '" + name() + "'" + " [" +
         position().ToDebugString() + "]";
}
