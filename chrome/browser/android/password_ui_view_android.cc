// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/password_ui_view_android.h"

#include <algorithm>

#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "base/android/jni_weak_ref.h"
#include "base/android/scoped_java_ref.h"
#include "base/bind_helpers.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_piece.h"
#include "base/task_scheduler/post_task.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "components/autofill/core/common/password_form.h"
#include "components/browser_sync/profile_sync_service.h"
#include "components/password_manager/core/browser/export/password_csv_writer.h"
#include "components/password_manager/core/browser/password_manager_constants.h"
#include "components/password_manager/core/browser/password_ui_utils.h"
#include "components/password_manager/core/browser/ui/credential_provider_interface.h"
#include "content/public/browser/browser_thread.h"
#include "jni/PasswordUIView_jni.h"

using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

PasswordUIViewAndroid::PasswordUIViewAndroid(JNIEnv* env, jobject obj)
    : password_manager_presenter_(this), weak_java_ui_controller_(env, obj) {}

PasswordUIViewAndroid::~PasswordUIViewAndroid() {}

void PasswordUIViewAndroid::Destroy(JNIEnv*, const JavaParamRef<jobject>&) {
  switch (state_) {
    case State::ALIVE:
      delete this;
      break;
    case State::ALIVE_SERIALIZATION_PENDING:
      // Postpone the deletion until the pending tasks are completed, so that
      // the tasks do not attempt a use after free while reading data from
      // |this|.
      state_ = State::DELETION_PENDING;
      break;
    case State::DELETION_PENDING:
      NOTREACHED();
      break;
  }
}

Profile* PasswordUIViewAndroid::GetProfile() {
  return ProfileManager::GetLastUsedProfile();
}

void PasswordUIViewAndroid::ShowPassword(size_t index,
                                         const base::string16& password_value) {
  NOTIMPLEMENTED();
}

void PasswordUIViewAndroid::SetPasswordList(
    const std::vector<std::unique_ptr<autofill::PasswordForm>>& password_list) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> ui_controller = weak_java_ui_controller_.get(env);
  if (!ui_controller.is_null()) {
    Java_PasswordUIView_passwordListAvailable(
        env, ui_controller, static_cast<int>(password_list.size()));
  }
}

void PasswordUIViewAndroid::SetPasswordExceptionList(
    const std::vector<std::unique_ptr<autofill::PasswordForm>>&
        password_exception_list) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> ui_controller = weak_java_ui_controller_.get(env);
  if (!ui_controller.is_null()) {
    Java_PasswordUIView_passwordExceptionListAvailable(
        env, ui_controller, static_cast<int>(password_exception_list.size()));
  }
}

void PasswordUIViewAndroid::UpdatePasswordLists(JNIEnv* env,
                                                const JavaParamRef<jobject>&) {
  DCHECK_EQ(State::ALIVE, state_);
  password_manager_presenter_.UpdatePasswordLists();
}

ScopedJavaLocalRef<jobject> PasswordUIViewAndroid::GetSavedPasswordEntry(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    int index) {
  DCHECK_EQ(State::ALIVE, state_);
  const autofill::PasswordForm* form =
      password_manager_presenter_.GetPassword(index);
  if (!form) {
    return Java_PasswordUIView_createSavedPasswordEntry(
        env, ConvertUTF8ToJavaString(env, std::string()),
        ConvertUTF16ToJavaString(env, base::string16()),
        ConvertUTF16ToJavaString(env, base::string16()));
  }
  return Java_PasswordUIView_createSavedPasswordEntry(
      env,
      ConvertUTF8ToJavaString(
          env, password_manager::GetShownOriginAndLinkUrl(*form).first),
      ConvertUTF16ToJavaString(env, form->username_value),
      ConvertUTF16ToJavaString(env, form->password_value));
}

ScopedJavaLocalRef<jstring> PasswordUIViewAndroid::GetSavedPasswordException(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    int index) {
  DCHECK_EQ(State::ALIVE, state_);
  const autofill::PasswordForm* form =
      password_manager_presenter_.GetPasswordException(index);
  if (!form)
    return ConvertUTF8ToJavaString(env, std::string());
  return ConvertUTF8ToJavaString(
      env, password_manager::GetShownOriginAndLinkUrl(*form).first);
}

void PasswordUIViewAndroid::HandleRemoveSavedPasswordEntry(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    int index) {
  DCHECK_EQ(State::ALIVE, state_);
  password_manager_presenter_.RemoveSavedPassword(index);
}

void PasswordUIViewAndroid::HandleRemoveSavedPasswordException(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    int index) {
  DCHECK_EQ(State::ALIVE, state_);
  password_manager_presenter_.RemovePasswordException(index);
}

void PasswordUIViewAndroid::HandleSerializePasswords(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>&,
    const base::android::JavaParamRef<jobject>& callback) {
  switch (state_) {
    case State::ALIVE:
      state_ = State::ALIVE_SERIALIZATION_PENDING;
      break;
    case State::ALIVE_SERIALIZATION_PENDING:
      // The UI should not allow the user to re-request export before finishing
      // or cancelling the pending one.
      NOTREACHED();
      return;
    case State::DELETION_PENDING:
      // The Java part should not first request destroying of |this| and then
      // ask |this| for serialized passwords.
      NOTREACHED();
      return;
  }
  // The tasks are posted with base::Unretained, because deletion is postponed
  // until the reply arrives (look for the occurrences of DELETION_PENDING in
  // this file). The background processing is not expected to take very long,
  // but still long enough not to block the UI thread. The main concern here is
  // not to avoid the background computation if |this| is about to be deleted
  // but to simply avoid use after free from the background task runner.
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&PasswordUIViewAndroid::ObtainAndSerializePasswords,
                     base::Unretained(this)),
      base::BindOnce(
          &PasswordUIViewAndroid::PostSerializedPasswords,
          base::Unretained(this),
          base::android::ScopedJavaGlobalRef<jobject>(env, callback)));
}

ScopedJavaLocalRef<jstring> JNI_PasswordUIView_GetAccountDashboardURL(
    JNIEnv* env,
    const JavaParamRef<jclass>&) {
  return ConvertUTF8ToJavaString(
      env, password_manager::kPasswordManagerAccountDashboardURL);
}

// static
static jlong JNI_PasswordUIView_Init(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj) {
  PasswordUIViewAndroid* controller = new PasswordUIViewAndroid(env, obj);
  return reinterpret_cast<intptr_t>(controller);
}

std::string PasswordUIViewAndroid::ObtainAndSerializePasswords() {
  // This is run on a backend task runner. Do not access any member variables
  // except for |credential_provider_| and |password_manager_presenter_|.
  password_manager::CredentialProviderInterface* const provider =
      credential_provider_for_testing_ ? credential_provider_for_testing_
                                       : &password_manager_presenter_;

  return password_manager::PasswordCSVWriter::SerializePasswords(
      provider->GetAllPasswords());
}

void PasswordUIViewAndroid::PostSerializedPasswords(
    const base::android::JavaRef<jobject>& callback,
    std::string serialized_passwords) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  switch (state_) {
    case State::ALIVE:
      NOTREACHED();
      break;
    case State::ALIVE_SERIALIZATION_PENDING: {
      state_ = State::ALIVE;
      if (export_target_for_testing_) {
        *export_target_for_testing_ = serialized_passwords;
      } else {
        JNIEnv* env = base::android::AttachCurrentThread();
        base::android::RunCallbackAndroid(
            callback, ConvertUTF8ToJavaString(env, serialized_passwords));
      }
      break;
    }
    case State::DELETION_PENDING:
      delete this;
      break;
  }
}
