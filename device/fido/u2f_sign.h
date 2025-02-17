// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_FIDO_U2F_SIGN_H_
#define DEVICE_FIDO_U2F_SIGN_H_

#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/optional.h"
#include "device/fido/sign_response_data.h"
#include "device/fido/u2f_request.h"
#include "device/fido/u2f_transport_protocol.h"

namespace service_manager {
class Connector;
}

namespace device {

class U2fSign : public U2fRequest {
 public:
  using SignResponseCallback =
      base::OnceCallback<void(U2fReturnCode status_code,
                              base::Optional<SignResponseData> response_data)>;

  U2fSign(std::string relying_party_id,
          service_manager::Connector* connector,
          const base::flat_set<U2fTransportProtocol>& protocols,
          const std::vector<std::vector<uint8_t>>& registered_keys,
          const std::vector<uint8_t>& challenge_hash,
          const std::vector<uint8_t>& app_param,
          SignResponseCallback completion_callback);
  ~U2fSign() override;

  static std::unique_ptr<U2fRequest> TrySign(
      std::string relying_party_id,
      service_manager::Connector* connector,
      const base::flat_set<U2fTransportProtocol>& protocols,
      const std::vector<std::vector<uint8_t>>& registered_keys,
      const std::vector<uint8_t>& challenge_hash,
      const std::vector<uint8_t>& app_param,
      SignResponseCallback completion_callback);

 private:
  void TryDevice() override;
  void OnTryDevice(std::vector<std::vector<uint8_t>>::const_iterator it,
                   U2fReturnCode return_code,
                   const std::vector<uint8_t>& response_data);

  const std::vector<std::vector<uint8_t>> registered_keys_;
  std::vector<uint8_t> challenge_hash_;
  std::vector<uint8_t> app_param_;
  SignResponseCallback completion_callback_;

  base::WeakPtrFactory<U2fSign> weak_factory_;
};

}  // namespace device

#endif  // DEVICE_FIDO_U2F_SIGN_H_
