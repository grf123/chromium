// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_CONTENT_VERIFIER_TEST_UTILS_H_
#define EXTENSIONS_BROWSER_CONTENT_VERIFIER_TEST_UTILS_H_

#include "base/files/file_path.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "extensions/browser/content_verify_job.h"
#include "extensions/common/extension_id.h"

namespace extensions {

class Extension;

// Test class to observe a particular extension resource's ContentVerifyJob
// lifetime.  Provides a way to wait for a job to finish and return
// the job's result.
class TestContentVerifyJobObserver : ContentVerifyJob::TestObserver {
 public:
  TestContentVerifyJobObserver(const ExtensionId& extension_id,
                               const base::FilePath& relative_path);
  ~TestContentVerifyJobObserver();

  // ContentVerifyJob::TestObserver:
  void JobStarted(const ExtensionId& extension_id,
                  const base::FilePath& relative_path) override {}
  void JobFinished(const ExtensionId& extension_id,
                   const base::FilePath& relative_path,
                   ContentVerifyJob::FailureReason reason) override;
  void OnHashesReady(const ExtensionId& extension_id,
                     const base::FilePath& relative_path,
                     bool success) override;

  // Waits for a ContentVerifyJob to finish and returns job's status.
  ContentVerifyJob::FailureReason WaitForJobFinished() WARN_UNUSED_RESULT;

  // Waits for ContentVerifyJob to finish the attempt to read content hashes.
  void WaitForOnHashesReady();

 private:
  base::RunLoop job_finished_run_loop_;
  base::RunLoop on_hashes_ready_run_loop_;

  ExtensionId extension_id_;
  base::FilePath relative_path_;
  base::Optional<ContentVerifyJob::FailureReason> failure_reason_;
  bool seen_on_hashes_ready_ = false;

  DISALLOW_COPY_AND_ASSIGN(TestContentVerifyJobObserver);
};

namespace content_verifier_test_utils {

// Unzips the extension source from |extension_zip| into |unzip_dir|
// directory and loads it. Returns the resulting Extension object.
// |destination| points to the path where the extension was extracted.
//
// TODO(lazyboy): Move this function to a generic file.
scoped_refptr<Extension> UnzipToDirAndLoadExtension(
    const base::FilePath& extension_zip,
    const base::FilePath& unzip_dir);

}  // namespace content_verifier_test_utils

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_CONTENT_VERIFIER_TEST_UTILS_H_
