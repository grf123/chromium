// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/cryptohome/cryptohome_util.h"

#include <string>

#include "base/logging.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/dbus/cryptohome/key.pb.h"
#include "chromeos/dbus/cryptohome/rpc.pb.h"
#include "components/device_event_log/device_event_log.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace cryptohome {

namespace {

bool IsEmpty(const base::Optional<BaseReply>& reply) {
  if (!reply.has_value()) {
    LOGIN_LOG(ERROR) << "Cryptohome call failed with empty reply.";
    return true;
  }
  return false;
}

void KeyDefPrivilegesToKeyPrivileges(int key_def_privileges,
                                     KeyPrivileges* privileges) {
  privileges->set_mount(key_def_privileges & PRIV_MOUNT);
  privileges->set_add(key_def_privileges & PRIV_ADD);
  privileges->set_remove(key_def_privileges & PRIV_REMOVE);
  privileges->set_update(key_def_privileges & PRIV_MIGRATE);
  privileges->set_authorized_update(key_def_privileges &
                                    PRIV_AUTHORIZED_UPDATE);
}

// TODO(crbug.com/797848): Add tests that cover this logic.
void KeyDefSecretToKeyAuthorizationSecret(
    const KeyDefinition::AuthorizationData::Secret& key_def_secret,
    KeyAuthorizationSecret* secret) {
  secret->mutable_usage()->set_encrypt(key_def_secret.encrypt);
  secret->mutable_usage()->set_sign(key_def_secret.sign);
  secret->set_wrapped(key_def_secret.wrapped);
  if (!key_def_secret.symmetric_key.empty())
    secret->set_symmetric_key(key_def_secret.symmetric_key);

  if (!key_def_secret.public_key.empty())
    secret->set_public_key(key_def_secret.public_key);
}

// TODO(crbug.com/797848): Add tests that cover this logic.
void KeyDefProviderDataToKeyProviderDataEntry(
    const KeyDefinition::ProviderData& provider_data,
    KeyProviderData::Entry* entry) {
  entry->set_name(provider_data.name);
  if (provider_data.number)
    entry->set_number(*provider_data.number);

  if (provider_data.bytes)
    entry->set_bytes(*provider_data.bytes);
}

// TODO(crbug.com/797848): Add tests that cover this logic.
KeyAuthorizationData::KeyAuthorizationType GetKeyAuthDataType(
    KeyDefinition::AuthorizationData::Type key_def_auth_data_type) {
  switch (key_def_auth_data_type) {
    case KeyDefinition::AuthorizationData::TYPE_HMACSHA256:
      return KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256;
    case KeyDefinition::AuthorizationData::TYPE_AES256CBC_HMACSHA256:
      return KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_AES256CBC_HMACSHA256;
  }
}

}  // namespace

MountError MountExReplyToMountError(const base::Optional<BaseReply>& reply) {
  if (IsEmpty(reply))
    return MOUNT_ERROR_FATAL;

  if (!reply->HasExtension(MountReply::reply)) {
    LOGIN_LOG(ERROR) << "MountEx failed with no MountReply extension in reply.";
    return MOUNT_ERROR_FATAL;
  }
  return CryptohomeErrorToMountError(reply->error());
}

MountError BaseReplyToMountError(const base::Optional<BaseReply>& reply) {
  if (IsEmpty(reply))
    return MOUNT_ERROR_FATAL;

  return CryptohomeErrorToMountError(reply->error());
}

const std::string& MountExReplyToMountHash(const BaseReply& reply) {
  return reply.GetExtension(MountReply::reply).sanitized_username();
}

AuthorizationRequest CreateAuthorizationRequest(const std::string& label,
                                                const std::string& secret) {
  cryptohome::AuthorizationRequest auth_request;
  Key* key = auth_request.mutable_key();
  if (!label.empty())
    key->mutable_data()->set_label(label);

  key->set_secret(secret);
  return auth_request;
}

// TODO(crbug.com/797848): Finish testing this method.
void KeyDefinitionToKey(const KeyDefinition& key_def, Key* key) {
  key->set_secret(key_def.secret);
  KeyData* data = key->mutable_data();
  DCHECK_EQ(KeyDefinition::TYPE_PASSWORD, key_def.type);
  data->set_type(KeyData::KEY_TYPE_PASSWORD);
  data->set_label(key_def.label);

  if (key_def.revision > 0)
    data->set_revision(key_def.revision);

  if (key_def.privileges != 0) {
    KeyDefPrivilegesToKeyPrivileges(key_def.privileges,
                                    data->mutable_privileges());
  }

  for (const auto& key_def_auth_data : key_def.authorization_data) {
    KeyAuthorizationData* auth_data = data->add_authorization_data();
    auth_data->set_type(GetKeyAuthDataType(key_def_auth_data.type));
    for (const auto& key_def_secret : key_def_auth_data.secrets) {
      KeyDefSecretToKeyAuthorizationSecret(key_def_secret,
                                           auth_data->add_secrets());
    }
  }

  for (const auto& provider_data : key_def.provider_data) {
    KeyDefProviderDataToKeyProviderDataEntry(
        provider_data, data->mutable_provider_data()->add_entry());
  }
}

// TODO(crbug.com/797848): Finish testing this method.
MountError CryptohomeErrorToMountError(CryptohomeErrorCode code) {
  switch (code) {
    case CRYPTOHOME_ERROR_NOT_SET:
      return MOUNT_ERROR_NONE;
    case CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND:
      return MOUNT_ERROR_USER_DOES_NOT_EXIST;
    case CRYPTOHOME_ERROR_NOT_IMPLEMENTED:
    case CRYPTOHOME_ERROR_MOUNT_FATAL:
    case CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED:
    case CRYPTOHOME_ERROR_BACKING_STORE_FAILURE:
      return MOUNT_ERROR_FATAL;
    case CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND:
    case CRYPTOHOME_ERROR_KEY_NOT_FOUND:
    case CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED:
      return MOUNT_ERROR_KEY_FAILURE;
    case CRYPTOHOME_ERROR_TPM_COMM_ERROR:
      return MOUNT_ERROR_TPM_COMM_ERROR;
    case CRYPTOHOME_ERROR_TPM_DEFEND_LOCK:
      return MOUNT_ERROR_TPM_DEFEND_LOCK;
    case CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY:
      return MOUNT_ERROR_MOUNT_POINT_BUSY;
    case CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT:
      return MOUNT_ERROR_TPM_NEEDS_REBOOT;
    case CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED:
    case CRYPTOHOME_ERROR_KEY_LABEL_EXISTS:
    case CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID:
      return MOUNT_ERROR_KEY_FAILURE;
    case CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION:
      return MOUNT_ERROR_OLD_ENCRYPTION;
    case CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE:
      return MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE;
    // TODO(crbug.com/797563): Split the error space and/or handle everything.
    case CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID:
    case CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN:
    case CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND:
    case CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN:
    case CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE:
    case CRYPTOHOME_ERROR_ATTESTATION_NOT_READY:
    case CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA:
    case CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT:
    case CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE:
    case CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR:
    case CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID:
    case CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE:
    case CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE:
      NOTREACHED();
      return MOUNT_ERROR_FATAL;
  }
}

void KeyAuthorizationDataToAuthorizationData(
    const KeyAuthorizationData& authorization_data_proto,
    KeyDefinition::AuthorizationData* authorization_data) {
  switch (authorization_data_proto.type()) {
    case KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256:
      authorization_data->type =
          KeyDefinition::AuthorizationData::TYPE_HMACSHA256;
      break;
    case KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_AES256CBC_HMACSHA256:
      authorization_data->type =
          KeyDefinition::AuthorizationData::TYPE_AES256CBC_HMACSHA256;
      break;
  }

  for (const auto& secret : authorization_data_proto.secrets()) {
    authorization_data->secrets.push_back(
        KeyDefinition::AuthorizationData::Secret(
            secret.usage().encrypt(), secret.usage().sign(),
            secret.symmetric_key(), secret.public_key(), secret.wrapped()));
  }
}

}  // namespace cryptohome
