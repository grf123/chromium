// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/browser/cast_web_view_extension.h"

#include "base/logging.h"
#include "chromecast/browser/cast_browser_process.h"
#include "chromecast/browser/cast_extension_host.h"
#include "chromecast/browser/devtools/remote_debugging_server.h"
#include "extensions/browser/extension_system.h"

namespace chromecast {

CastWebViewExtension::CastWebViewExtension(
    const CreateParams& params,
    CastWebContentsManager* web_contents_manager,
    content::BrowserContext* browser_context,
    scoped_refptr<content::SiteInstance> site_instance,
    const extensions::Extension* extension,
    const GURL& initial_url)
    : window_(shell::CastContentWindow::Create(params.delegate,
                                               params.is_headless,
                                               params.enable_touch_input)),
      extension_host_(std::make_unique<CastExtensionHost>(
          browser_context,
          params.delegate,
          extension,
          initial_url,
          site_instance.get(),
          extensions::VIEW_TYPE_EXTENSION_POPUP)),
      remote_debugging_server_(
          shell::CastBrowserProcess::GetInstance()->remote_debugging_server()) {
  // If this CastWebView is enabled for development, start the remote debugger.
  if (params.enabled_for_dev) {
    LOG(INFO) << "Enabling dev console for " << web_contents()->GetVisibleURL();
    remote_debugging_server_->EnableWebContentsForDebugging(web_contents());
  }
}

CastWebViewExtension::~CastWebViewExtension() {}

shell::CastContentWindow* CastWebViewExtension::window() const {
  return window_.get();
}

content::WebContents* CastWebViewExtension::web_contents() const {
  return extension_host_->host_contents();
}

void CastWebViewExtension::LoadUrl(GURL url) {
  extension_host_->CreateRenderViewSoon();
}

void CastWebViewExtension::ClosePage(const base::TimeDelta& shutdown_delay) {}

void CastWebViewExtension::Show(CastWindowManager* window_manager) {
  window_->ShowWebContents(web_contents(), window_manager);
  web_contents()->Focus();
}

}  // namespace chromecast
