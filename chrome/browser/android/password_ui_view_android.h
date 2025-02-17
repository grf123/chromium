// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_PASSWORD_UI_VIEW_ANDROID_H_
#define CHROME_BROWSER_ANDROID_PASSWORD_UI_VIEW_ANDROID_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/android/jni_weak_ref.h"
#include "base/android/scoped_java_ref.h"
#include "base/macros.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/ui/passwords/password_manager_presenter.h"
#include "chrome/browser/ui/passwords/password_ui_view.h"
#include "components/password_manager/core/browser/password_store.h"
#include "components/password_manager/core/browser/password_store_consumer.h"

namespace password_manager {
class CredentialProviderInterface;
}

// PasswordUIView for Android, contains jni hooks that allows Android UI to
// display passwords and route UI commands back to native
// PasswordManagerPresenter.
class PasswordUIViewAndroid : public PasswordUIView {
 public:
  PasswordUIViewAndroid(JNIEnv* env, jobject);
  ~PasswordUIViewAndroid() override;

  // PasswordUIView implementation.
  Profile* GetProfile() override;
  void ShowPassword(size_t index,
                    const base::string16& password_value) override;
  void SetPasswordList(
      const std::vector<std::unique_ptr<autofill::PasswordForm>>& password_list)
      override;
  void SetPasswordExceptionList(
      const std::vector<std::unique_ptr<autofill::PasswordForm>>&
          password_exception_list) override;

  // Calls from Java.
  base::android::ScopedJavaLocalRef<jobject> GetSavedPasswordEntry(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      int index);
  base::android::ScopedJavaLocalRef<jstring> GetSavedPasswordException(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      int index);
  void UpdatePasswordLists(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>&);
  void HandleRemoveSavedPasswordEntry(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      int index);
  void HandleRemoveSavedPasswordException(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      int index);
  void HandleSerializePasswords(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      const base::android::JavaParamRef<jobject>& callback);
  // Destroy the native implementation.
  void Destroy(JNIEnv*, const base::android::JavaParamRef<jobject>&);

  void set_export_target_for_testing(std::string* export_target_for_testing) {
    export_target_for_testing_ = export_target_for_testing;
  }

  void set_credential_provider_for_testing(
      password_manager::CredentialProviderInterface* provider) {
    credential_provider_for_testing_ = provider;
  }

 private:
  // Possible states in the life of PasswordUIViewAndroid.
  // ALIVE:
  //   * Destroy was not called and no background tasks are pending.
  //   * All data members can be used on the main task runner.
  // ALIVE_SERIALIZATION_PENDING:
  //   * Destroy was not called, password serialization task on another task
  //     runner is running.
  //   * All data members can be used on the main task runner, except for
  //     |password_manager_presenter_| which can only be used inside
  //     ObtainAndSerializePasswords, which is being run on a backend task
  //     runner.
  // DELETION_PENDING:
  //   * Destroy() was called, a background task is pending and |this| should
  //     be deleted once the tasks complete.
  //   * This state should not be reached anywhere but in the compeltion call
  //     of the pending task.
  enum class State { ALIVE, ALIVE_SERIALIZATION_PENDING, DELETION_PENDING };

  // Calls |password_manager_presenter_| to retrieve cached PasswordForm objects
  // and then PasswordCSVWriter to serialize them. It returns the serialized
  // value. Both steps involve a lot of memory allocation and copying, so this
  // method should be executed on a suitable task runner.
  std::string ObtainAndSerializePasswords();

  // Sends |serialized_passwords| to Java via |callback|.
  void PostSerializedPasswords(const base::android::JavaRef<jobject>& callback,
                               std::string serialized_passwords);

  // The |state_| must only be accessed on the main task runner.
  State state_ = State::ALIVE;

  // If not null, PostSerializedPasswords will write the serialized passwords to
  // |*export_target_for_testing_| instead of passing them to Java. This must
  // remain null in production code.
  std::string* export_target_for_testing_ = nullptr;

  PasswordManagerPresenter password_manager_presenter_;

  // If not null, passwords for exporting will be obtained from
  // |*credential_provider_for_testing_|, otherwise from
  // |password_manager_presenter_|. This must remain null in production code.
  password_manager::CredentialProviderInterface*
      credential_provider_for_testing_ = nullptr;

  // Java side of UI controller.
  JavaObjectWeakGlobalRef weak_java_ui_controller_;

  DISALLOW_COPY_AND_ASSIGN(PasswordUIViewAndroid);
};

#endif  // CHROME_BROWSER_ANDROID_PASSWORD_UI_VIEW_ANDROID_H_
