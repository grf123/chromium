// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_
#define CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_

#include <map>
#include <memory>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/conflicts/installed_programs_win.h"
#include "chrome/browser/conflicts/module_info_win.h"
#include "chrome/browser/conflicts/module_inspector_win.h"
#include "chrome/browser/conflicts/module_list_manager_win.h"
#include "content/public/common/process_type.h"

class ModuleDatabaseObserver;
class ModuleListFilter;
class ProblematicProgramsUpdater;
class ThirdPartyMetricsRecorder;

namespace base {
class FilePath;
}

// A class that keeps track of all modules loaded across Chrome processes.
//
// It is also the main class behind third-party modules tracking, and owns the
// different classes that required to identify problematic programs and
// record metrics.
//
// This is effectively a singleton, but doesn't use base::Singleton. The intent
// is for the object to be created when Chrome is single-threaded, and for it
// be set as the process-wide singleton via SetInstance.
class ModuleDatabase : ModuleListManager::Observer {
 public:
  // Structures for maintaining information about modules.
  using ModuleMap = std::map<ModuleInfoKey, ModuleInfoData>;
  using ModuleInfo = ModuleMap::value_type;

  // The Module Database becomes idle after this timeout expires without any
  // module events.
  static constexpr base::TimeDelta kIdleTimeout =
      base::TimeDelta::FromSeconds(10);

  // A ModuleDatabase is by default bound to a provided sequenced task runner.
  // All calls must be made in the context of this task runner, unless
  // otherwise noted. For calls from other contexts this task runner is used to
  // bounce the call when appropriate.
  explicit ModuleDatabase(scoped_refptr<base::SequencedTaskRunner> task_runner);
  ~ModuleDatabase() override;

  // Retrieves the singleton global instance of the ModuleDatabase.
  static ModuleDatabase* GetInstance();

  // Sets the global instance of the ModuleDatabase. Ownership is passed to the
  // global instance and deliberately leaked, unless manually cleaned up. This
  // has no locking and should be called when Chrome is single threaded.
  static void SetInstance(std::unique_ptr<ModuleDatabase> module_database);

  // Returns true if the ModuleDatabase is idle. This means that no modules are
  // currently being inspected, and no new module events have been observed in
  // the last 10 seconds.
  bool IsIdle();

  // Indicates that a new registered shell extension was found. Must be called
  // in the same sequence as |task_runner_|.
  void OnShellExtensionEnumerated(const base::FilePath& path,
                                  uint32_t size_of_image,
                                  uint32_t time_date_stamp);

  // Indicates that all shell extensions have been enumerated.
  void OnShellExtensionEnumerationFinished();

  // Indicates that a new registered input method editor was found. Must be
  // called in the same sequence as |task_runner_|.
  void OnImeEnumerated(const base::FilePath& path,
                       uint32_t size_of_image,
                       uint32_t time_date_stamp);

  // Indicates that all input method editors have been enumerated.
  void OnImeEnumerationFinished();

  // ModuleListManager::Observer:
  void OnNewModuleList(const base::Version& version,
                       const base::FilePath& path) override;

  // Indicates that a module has been loaded. The data passed to this function
  // is taken as gospel, so if it originates from a remote process it should be
  // independently validated first. (In practice, see ModuleEventSinkImpl for
  // details of where this happens.)
  void OnModuleLoad(content::ProcessType process_type,
                    const base::FilePath& module_path,
                    uint32_t module_size,
                    uint32_t module_time_date_stamp,
                    uintptr_t module_load_address);

  // TODO(chrisha): Module analysis code, and various accessors for use by
  // chrome://conflicts.

  // Adds or removes an observer.
  // Note that when adding an observer, OnNewModuleFound() will immediately be
  // called once for all modules that are already loaded before returning to the
  // caller. In addition, if the ModuleDatabase is currently idle,
  // OnModuleDatabaseIdle() will also be invoked.
  // Must be called in the same sequence as |task_runner_|, and all
  // notifications will be sent on that same task runner.
  void AddObserver(ModuleDatabaseObserver* observer);
  void RemoveObserver(ModuleDatabaseObserver* observer);

  // Raises the priority of module inspection tasks to ensure the
  // ModuleDatabase becomes idle ASAP.
  void IncreaseInspectionPriority();

  // Accessor for the module list manager. This is exposed so that the manager
  // can be wired up to the ThirdPartyModuleListComponentInstaller.
  ModuleListManager& module_list_manager() { return module_list_manager_; }

 private:
  friend class TestModuleDatabase;
  friend class ModuleDatabaseTest;
  friend class ModuleEventSinkImplTest;

  // Converts a valid |process_type| to a bit for use in a bitmask of process
  // values. Exposed in the header for testing.
  static uint32_t ProcessTypeToBit(content::ProcessType process_type);

  // Converts a |bit_index| (which maps to the bit 1 << bit_index) to the
  // corresponding process type. Exposed in the header for testing.
  static content::ProcessType BitIndexToProcessType(uint32_t bit_index);

  // Finds or creates a mutable ModuleInfo entry.
  ModuleInfo* FindOrCreateModuleInfo(const base::FilePath& module_path,
                                     uint32_t module_size,
                                     uint32_t module_time_date_stamp);

  // Returns true if the enumeration of the IMEs and the shell extensions is
  // finished.
  //
  // To avoid sending an improperly tagged module to an observer (in case a race
  // condition happens and the module is loaded before the enumeration is done),
  // it's important that this function returns true before any calls to
  // OnNewModuleFound() is made.
  bool RegisteredModulesEnumerated();

  // Called when RegisteredModulesEnumerated() becomes true. Notifies the
  // observers of each already inspected modules and checks if the idle state
  // should be entered.
  void OnRegisteredModulesEnumerated();

  // Callback for ModuleInspector.
  void OnModuleInspected(
      const ModuleInfoKey& module_key,
      std::unique_ptr<ModuleInspectionResult> inspection_result);

  // If the ModuleDatabase is truly idle, calls EnterIdleState().
  void OnDelayExpired();

  // Notifies the observers that ModuleDatabase is now idle.
  void EnterIdleState();

  // Notifies the |observer| of already found and inspected modules via
  // OnNewModuleFound().
  void NotifyLoadedModules(ModuleDatabaseObserver* observer);

  // Called when |installed_programs_| finishes its initialization.
  void OnInstalledProgramsInitialized();

  // Initializes |problematic_programs_updater_| when both the ModuleListFilter
  // and the InstalledPrograms are available.
  void InitializeProblematicProgramsUpdater();

  // The task runner to which this object is bound.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // A map of all known modules.
  ModuleMap modules_;

  base::Timer idle_timer_;

  // Indicates if the ModuleDatabase has started processing module load events.
  bool has_started_processing_;

  // Indicates if all shell extensions have been enumerated.
  bool shell_extensions_enumerated_;

  // Indicates if all input method editors have been enumerated.
  bool ime_enumerated_;

  // Indicates if the initial Module List has been received.
  bool module_list_received_;

  // Inspects new modules on a blocking task runner.
  ModuleInspector module_inspector_;

  // Holds observers.
  base::ObserverList<ModuleDatabaseObserver> observer_list_;

  // Keeps track of where the most recent module list is located on disk, and
  // provides notifications when this changes.
  ModuleListManager module_list_manager_;

  // Filters third-party modules against a whitelist and a blacklist.
  std::unique_ptr<ModuleListFilter> module_list_filter_;

  // Retrieves the list of installed programs.
  InstalledPrograms installed_programs_;

  // Records metrics on third-party modules.
  std::unique_ptr<ThirdPartyMetricsRecorder> third_party_metrics_;

  // Maintains the cache of problematic programs.
  std::unique_ptr<ProblematicProgramsUpdater> problematic_programs_updater_;

  // Weak pointer factory for this object. This is used when bouncing
  // incoming events to |task_runner_|.
  base::WeakPtrFactory<ModuleDatabase> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ModuleDatabase);
};

#endif  // CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_
