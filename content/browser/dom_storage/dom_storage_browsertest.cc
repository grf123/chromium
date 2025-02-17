// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/test/bind_test_util.h"
#include "build/build_config.h"
#include "content/browser/dom_storage/dom_storage_context_wrapper.h"
#include "content/browser/dom_storage/local_storage_context_mojo.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/dom_storage/dom_storage_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_launcher.h"
#include "content/shell/browser/shell.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace content {

// This browser test is aimed towards exercising the DOMStorage system
// from end-to-end.
class DOMStorageBrowserTest : public ContentBrowserTest {
 public:
  DOMStorageBrowserTest() {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kDisableMojoLocalStorage);
  }

  void SimpleTest(const GURL& test_url, bool incognito) {
    // The test page will perform tests then navigate to either
    // a #pass or #fail ref.
    Shell* the_browser = incognito ? CreateOffTheRecordBrowser() : shell();
    NavigateToURLBlockUntilNavigationsComplete(the_browser, test_url, 2);
    std::string result =
        the_browser->web_contents()->GetLastCommittedURL().ref();
    if (result != "pass") {
      std::string js_result;
      ASSERT_TRUE(ExecuteScriptAndExtractString(
          the_browser, "window.domAutomationController.send(getLog())",
          &js_result));
      FAIL() << "Failed: " << js_result;
    }
  }

  std::vector<LocalStorageUsageInfo> GetUsage() {
    auto* context = BrowserContext::GetDefaultStoragePartition(
                        shell()->web_contents()->GetBrowserContext())
                        ->GetDOMStorageContext();
    base::RunLoop loop;
    std::vector<LocalStorageUsageInfo> usage;
    context->GetLocalStorageUsage(base::BindLambdaForTesting(
        [&](const std::vector<LocalStorageUsageInfo>& u) {
          usage = u;
          loop.Quit();
        }));
    loop.Run();
    return usage;
  }

  void DeletePhysicalOrigin(GURL origin) {
    auto* context = BrowserContext::GetDefaultStoragePartition(
                        shell()->web_contents()->GetBrowserContext())
                        ->GetDOMStorageContext();
    base::RunLoop loop;
    context->DeleteLocalStorageForPhysicalOrigin(origin, loop.QuitClosure());
    loop.Run();
  }
};

class MojoDOMStorageBrowserTest : public DOMStorageBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
  }

  LocalStorageContextMojo* context() {
    return static_cast<DOMStorageContextWrapper*>(
               BrowserContext::GetDefaultStoragePartition(
                   shell()->web_contents()->GetBrowserContext())
                   ->GetDOMStorageContext())
        ->mojo_state_;
  }

  void EnsureConnected() {
    base::RunLoop run_loop;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::BindOnce(
            &LocalStorageContextMojo::RunWhenConnected,
            base::Unretained(context()),
            base::BindOnce(base::IgnoreResult(&base::TaskRunner::PostTask),
                           base::ThreadTaskRunnerHandle::Get(), FROM_HERE,
                           run_loop.QuitClosure())));
    run_loop.Run();
  }
};

static const bool kIncognito = true;
static const bool kNotIncognito = false;

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, SanityCheck) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, SanityCheckIncognito) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kIncognito);
}

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, PRE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

// http://crbug.com/654704 PRE_ tests aren't supported on Android.
#if defined(OS_ANDROID)
#define MAYBE_DataPersists DISABLED_DataPersists
#else
#define MAYBE_DataPersists DataPersists
#endif
IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, MAYBE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, SanityCheck) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, SanityCheckIncognito) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, PRE_DataPersists) {
  EnsureConnected();
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, MAYBE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, DeletePhysicalOrigin) {
  EXPECT_EQ(0U, GetUsage().size());
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
  std::vector<LocalStorageUsageInfo> usage = GetUsage();
  ASSERT_EQ(1U, usage.size());
  DeletePhysicalOrigin(usage[0].origin);
  EXPECT_EQ(0U, GetUsage().size());
}

// On Windows file://localhost/C:/src/chromium/src/content/test/data/title1.html
// doesn't work.
#if !defined(OS_WIN)
// Regression test for https://crbug.com/776160.  The test verifies that there
// is no disagreement between 1) site URL used for browser-side isolation
// enforcement and 2) the origin requested by Blink.  Before this bug was fixed,
// (1) was file://localhost/ and (2) was file:// - this led to renderer kills.
IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, FileUrlWithHost) {
  // Navigate to file://localhost/.../title1.html
  GURL regular_file_url = GetTestUrl(nullptr, "title1.html");
  GURL::Replacements host_replacement;
  host_replacement.SetHostStr("localhost");
  GURL file_with_host_url =
      regular_file_url.ReplaceComponents(host_replacement);
  EXPECT_TRUE(NavigateToURL(shell(), file_with_host_url));
  EXPECT_THAT(shell()->web_contents()->GetLastCommittedURL().spec(),
              testing::StartsWith("file://localhost/"));
  EXPECT_THAT(shell()->web_contents()->GetLastCommittedURL().spec(),
              testing::EndsWith("/title1.html"));

  // Verify that window.localStorage works fine.
  std::string result;
  std::string script = R"(
      localStorage["foo"] = "bar";
      domAutomationController.send(localStorage["foo"]);
  )";
  EXPECT_TRUE(ExecuteScriptAndExtractString(shell(), script, &result));
  EXPECT_EQ("bar", result);
}
#endif

class DOMStorageMigrationBrowserTest : public DOMStorageBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    // Only enable mojo local storage if this is not a PRE_ test.
    if (IsPreTest())
      command_line->AppendSwitch(switches::kDisableMojoLocalStorage);
  }
};

IN_PROC_BROWSER_TEST_F(DOMStorageMigrationBrowserTest, PRE_DataMigrates) {
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

// http://crbug.com/654704 PRE_ tests aren't supported on Android.
#if defined(OS_ANDROID)
#define MAYBE_DataMigrates DISABLED_DataMigrates
#else
#define MAYBE_DataMigrates DataMigrates
#endif
IN_PROC_BROWSER_TEST_F(DOMStorageMigrationBrowserTest, MAYBE_DataMigrates) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

}  // namespace content
