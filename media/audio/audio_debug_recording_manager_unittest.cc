// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/audio_debug_recording_manager.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/scoped_task_environment.h"
#include "media/audio/audio_debug_recording_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;

#if defined(OS_WIN)
#define IntToStringType base::IntToString16
#else
#define IntToStringType base::IntToString
#endif

namespace media {

namespace {

// The stream type expected to be added to file name.
const base::FilePath::CharType kStreamType[] = FILE_PATH_LITERAL("output");

// Used to be able to set call expectations in the MockAudioDebugRecordingHelper
// ctor. See also comment on the test EnableRegisterDisable.
bool g_expect_enable_after_create_helper = false;

// A helper struct to be able to set and unset
// |g_expect_enable_after_create_helper| scoped.
struct ScopedExpectEnableAfterCreateHelper {
  ScopedExpectEnableAfterCreateHelper() {
    CHECK(!g_expect_enable_after_create_helper);
    g_expect_enable_after_create_helper = true;
  }
  ~ScopedExpectEnableAfterCreateHelper() {
    CHECK(g_expect_enable_after_create_helper);
    g_expect_enable_after_create_helper = false;
  }
};

// Function bound and passed to AudioDebugRecordingManager::EnableDebugRecording
// as AudioDebugRecordingManager::CreateFileCallback.
void CreateFile(const base::FilePath& file_path,
                base::OnceCallback<void(base::File)>) {}

}  // namespace

// Mock class to verify enable and disable calls.
class MockAudioDebugRecordingHelper : public AudioDebugRecordingHelper {
 public:
  MockAudioDebugRecordingHelper(
      const AudioParameters& params,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      base::OnceClosure on_destruction_closure)
      : AudioDebugRecordingHelper(params,
                                  std::move(task_runner),
                                  base::OnceClosure()),
        on_destruction_closure_in_mock_(std::move(on_destruction_closure)) {
    if (g_expect_enable_after_create_helper)
      EXPECT_CALL(*this, DoEnableDebugRecording(_));
  }

  ~MockAudioDebugRecordingHelper() override {
    if (on_destruction_closure_in_mock_)
      std::move(on_destruction_closure_in_mock_).Run();
  }

  MOCK_METHOD1(DoEnableDebugRecording, void(const base::FilePath&));
  void EnableDebugRecording(const base::FilePath& file_name_suffix,
                            AudioDebugRecordingHelper::CreateFileCallback
                                create_file_callback) override {
    DoEnableDebugRecording(file_name_suffix);
  }

  MOCK_METHOD0(DisableDebugRecording, void());

 private:
  // We let the mock run the destruction closure to not rely on the real
  // implementation.
  base::OnceClosure on_destruction_closure_in_mock_;

  DISALLOW_COPY_AND_ASSIGN(MockAudioDebugRecordingHelper);
};

// Sub-class of the manager that overrides the CreateAudioDebugRecordingHelper
// function to create the above mock instead.
class AudioDebugRecordingManagerUnderTest : public AudioDebugRecordingManager {
 public:
  AudioDebugRecordingManagerUnderTest(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : AudioDebugRecordingManager(std::move(task_runner)) {}
  ~AudioDebugRecordingManagerUnderTest() override = default;

 private:
  std::unique_ptr<AudioDebugRecordingHelper> CreateAudioDebugRecordingHelper(
      const AudioParameters& params,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      base::OnceClosure on_destruction_closure) override {
    return std::make_unique<MockAudioDebugRecordingHelper>(
        params, std::move(task_runner),
        std::move(on_destruction_closure));
  }

  DISALLOW_COPY_AND_ASSIGN(AudioDebugRecordingManagerUnderTest);
};

// The test fixture.
class AudioDebugRecordingManagerTest : public ::testing::Test {
 public:
  AudioDebugRecordingManagerTest()
      : manager_(scoped_task_environment_.GetMainThreadTaskRunner()) {}

  ~AudioDebugRecordingManagerTest() override = default;

  // Registers a source and increases counter for the expected next source id.
  std::unique_ptr<AudioDebugRecorder> RegisterDebugRecordingSource(
      const AudioParameters& params) {
    ++expected_next_source_id_;
    return manager_.RegisterDebugRecordingSource(kStreamType, params);
  }

 protected:
  // The test task environment.
  base::test::ScopedTaskEnvironment scoped_task_environment_;

  AudioDebugRecordingManagerUnderTest manager_;

  // The expected next source id the manager will assign. It's static since the
  // manager uses a global running id, thus doesn't restart at each
  // instantiation.
  static int expected_next_source_id_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AudioDebugRecordingManagerTest);
};

int AudioDebugRecordingManagerTest::expected_next_source_id_ = 1;

// Shouldn't do anything but store the CreateFileCallback, i.e. no calls to
// recorders.
TEST_F(AudioDebugRecordingManagerTest, EnableDisable) {
  manager_.EnableDebugRecording(base::BindRepeating(&CreateFile));
  manager_.DisableDebugRecording();
}

// Tests registration and automatic unregistration on destruction of a recorder.
// The unregistration relies on that the MockAudioDebugRecordingHelper runs the
// |on_destruction_closure| given to it.
TEST_F(AudioDebugRecordingManagerTest, RegisterAutomaticUnregisterAtDelete) {
  const AudioParameters params;
  std::vector<std::unique_ptr<AudioDebugRecorder>> recorders;
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  EXPECT_EQ(3ul, recorders.size());
  EXPECT_EQ(recorders.size(), manager_.debug_recording_helpers_.size());

  while (!recorders.empty()) {
    recorders.pop_back();
    EXPECT_EQ(recorders.size(), manager_.debug_recording_helpers_.size());
  }
  EXPECT_EQ(0ul, recorders.size());
}

TEST_F(AudioDebugRecordingManagerTest, RegisterEnableDisable) {
  // Store away the extected id for the next source to use after registering all
  // sources.
  int expected_id = expected_next_source_id_;

  const AudioParameters params;
  std::vector<std::unique_ptr<AudioDebugRecorder>> recorders;
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  EXPECT_EQ(3ul, recorders.size());
  EXPECT_EQ(recorders.size(), manager_.debug_recording_helpers_.size());

  for (const auto& recorder : recorders) {
    MockAudioDebugRecordingHelper* mock_recording_helper =
        static_cast<MockAudioDebugRecordingHelper*>(recorder.get());
    base::FilePath expected_file_path =
        base::FilePath(kStreamType)
            .AddExtension(IntToStringType(expected_id++));
    EXPECT_CALL(*mock_recording_helper,
                DoEnableDebugRecording(expected_file_path));
    EXPECT_CALL(*mock_recording_helper, DisableDebugRecording());
  }

  manager_.EnableDebugRecording(base::BindRepeating(&CreateFile));
  manager_.DisableDebugRecording();
}

// Test enabling first, then registering. This should call enable on the
// recoders, but we can't set expectation for that since the mock object is
// created and called enable upon in RegisterDebugRecordingSource(), then
// returned. Instead expectation is set in the ctor of the mock by setting
// |g_expect_enable_after_create_helper| to true here (by using the scoped
// variable).
TEST_F(AudioDebugRecordingManagerTest, EnableRegisterDisable) {
  ScopedExpectEnableAfterCreateHelper scoped_enable_after_create_helper;

  manager_.EnableDebugRecording(base::BindRepeating(&CreateFile));

  const AudioParameters params;
  std::vector<std::unique_ptr<AudioDebugRecorder>> recorders;
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  recorders.push_back(RegisterDebugRecordingSource(params));
  EXPECT_EQ(3ul, recorders.size());
  EXPECT_EQ(recorders.size(), manager_.debug_recording_helpers_.size());

  for (const auto& recorder : recorders) {
    MockAudioDebugRecordingHelper* mock_recording_helper =
        static_cast<MockAudioDebugRecordingHelper*>(recorder.get());
    EXPECT_CALL(*mock_recording_helper, DisableDebugRecording());
  }

  manager_.DisableDebugRecording();
}

}  // namespace media
