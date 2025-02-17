// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_LOADER_SIGNED_EXCHANGE_SIGNATURE_VERIFIER_H_
#define CONTENT_BROWSER_LOADER_SIGNED_EXCHANGE_SIGNATURE_VERIFIER_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "content/common/content_export.h"
#include "net/http/http_response_headers.h"

namespace content {

// SignedExchangeSignatureVerifier verifies the signature of the given
// signed exchange. This is done by reconstructing the signed message
// and verifying the cryptographic signature enclosed in "Signature" response
// header.
//
// Note that SignedExchangeSignatureVerifier does not ensure the validity
// of the certificate used to generate the signature, which can't be done
// synchronously. (See SignedExchangeCertFetcher for this logic.)
//
// https://wicg.github.io/webpackage/draft-yasskin-http-origin-signed-responses.html#rfc.section.3.6
class CONTENT_EXPORT SignedExchangeSignatureVerifier final {
 public:
  struct CONTENT_EXPORT Input {
   public:
    Input();
    ~Input();

    std::string method;
    std::string url;
    int response_code;
    std::map<std::string, std::string> response_headers;
  };

  static bool Verify(const Input& input);

  static base::Optional<std::vector<uint8_t>> EncodeCanonicalExchangeHeaders(
      const Input& input);
};

}  // namespace content

#endif  // CONTENT_BROWSER_LOADER_SIGNED_EXCHANGE_SIGNATURE_VERIFIER_H_
