// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_BLOB_HANDLE_H_
#define CONTENT_PUBLIC_BROWSER_BLOB_HANDLE_H_

#include <string>
#include "third_party/WebKit/common/blob/blob.mojom.h"

namespace content {

// A handle to Blobs that can be stored outside of content/. This class holds
// a reference to the Blob and should be used to keep alive a Blob.
class BlobHandle {
 public:
  virtual ~BlobHandle() {}
  virtual std::string GetUUID() = 0;
  virtual blink::mojom::BlobPtr PassBlob() = 0;

 protected:
  BlobHandle() {}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_BLOB_HANDLE_H_
