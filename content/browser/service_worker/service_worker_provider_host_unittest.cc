// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_provider_host.h"

#include <memory>
#include <utility>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_dispatcher_host.h"
#include "content/browser/service_worker/service_worker_register_job.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/browser/service_worker/service_worker_test_utils.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/common/service_worker/service_worker_messages.h"
#include "content/common/url_schemes.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/origin_util.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "content/test/test_content_browser_client.h"
#include "content/test/test_content_client.h"
#include "mojo/edk/embedder/embedder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/common/service_worker/service_worker_object.mojom.h"
#include "third_party/WebKit/common/service_worker/service_worker_registration.mojom.h"

namespace content {

namespace {

const char kServiceWorkerScheme[] = "i-can-use-service-worker";

class ServiceWorkerTestContentClient : public TestContentClient {
 public:
  void AddAdditionalSchemes(Schemes* schemes) override {
    schemes->service_worker_schemes.push_back(kServiceWorkerScheme);
  }
};

class ServiceWorkerTestContentBrowserClient : public TestContentBrowserClient {
 public:
  struct AllowServiceWorkerCallLog {
    AllowServiceWorkerCallLog(const GURL& scope, const GURL& first_party)
        : scope(scope), first_party(first_party) {}
    const GURL scope;
    const GURL first_party;
  };

  ServiceWorkerTestContentBrowserClient() {}

  bool AllowServiceWorker(
      const GURL& scope,
      const GURL& first_party,
      content::ResourceContext* context,
      const base::Callback<WebContents*(void)>& wc_getter) override {
    logs_.emplace_back(scope, first_party);
    return false;
  }

  const std::vector<AllowServiceWorkerCallLog>& logs() const { return logs_; }

 private:
  std::vector<AllowServiceWorkerCallLog> logs_;
};

}  // namespace

class ServiceWorkerProviderHostTest : public testing::Test {
 protected:
  ServiceWorkerProviderHostTest()
      : thread_bundle_(TestBrowserThreadBundle::IO_MAINLOOP),
        next_renderer_provided_id_(1) {
    SetContentClient(&test_content_client_);
  }
  ~ServiceWorkerProviderHostTest() override {}

  void SetUp() override {
    old_content_browser_client_ =
        SetBrowserClientForTesting(&test_content_browser_client_);
    ResetSchemesAndOriginsWhitelist();
    mojo::edk::SetDefaultProcessErrorCallback(base::Bind(
        &ServiceWorkerProviderHostTest::OnMojoError, base::Unretained(this)));

    helper_.reset(new EmbeddedWorkerTestHelper(base::FilePath()));
    context_ = helper_->context();
    script_url_ = GURL("https://www.example.com/service_worker.js");

    blink::mojom::ServiceWorkerRegistrationOptions options1;
    options1.scope = GURL("https://www.example.com/");
    registration1_ =
        new ServiceWorkerRegistration(options1, 1L, context_->AsWeakPtr());

    blink::mojom::ServiceWorkerRegistrationOptions options2;
    options2.scope = GURL("https://www.example.com/example");
    registration2_ =
        new ServiceWorkerRegistration(options2, 2L, context_->AsWeakPtr());

    blink::mojom::ServiceWorkerRegistrationOptions options3;
    options3.scope = GURL("https://other.example.com/");
    registration3_ =
        new ServiceWorkerRegistration(options3, 3L, context_->AsWeakPtr());
  }

  void TearDown() override {
    registration1_ = nullptr;
    registration2_ = nullptr;
    registration3_ = nullptr;
    helper_.reset();
    SetBrowserClientForTesting(old_content_browser_client_);
    // Reset cached security schemes so we don't affect other tests.
    ResetSchemesAndOriginsWhitelist();
    mojo::edk::SetDefaultProcessErrorCallback(
        mojo::edk::ProcessErrorCallback());
  }

  ServiceWorkerRemoteProviderEndpoint PrepareServiceWorkerProviderHost(
      const GURL& document_url) {
    ServiceWorkerRemoteProviderEndpoint remote_endpoint;
    GURL topmost_frame_url = document_url;
    CreateProviderHostInternal(document_url, topmost_frame_url,
                               &remote_endpoint);
    return remote_endpoint;
  }

  ServiceWorkerRemoteProviderEndpoint
  PrepareServiceWorkerProviderHostWithTopmostFrameUrl(
      const GURL& document_url,
      const GURL& topmost_frame_url) {
    ServiceWorkerRemoteProviderEndpoint remote_endpoint;
    CreateProviderHostInternal(document_url, topmost_frame_url,
                               &remote_endpoint);
    return remote_endpoint;
  }

  ServiceWorkerProviderHost* CreateProviderHost(const GURL& document_url) {
    GURL topmost_frame_url = document_url;
    remote_endpoints_.emplace_back();
    return CreateProviderHostInternal(document_url, document_url,
                                      &remote_endpoints_.back());
  }

  ServiceWorkerProviderHost* CreateProviderHostWithInsecureParentFrame(
      const GURL& document_url) {
    remote_endpoints_.emplace_back();
    std::unique_ptr<ServiceWorkerProviderHost> host =
        CreateProviderHostForWindow(
            helper_->mock_render_process_id(), next_renderer_provided_id_++,
            false /* is_parent_frame_secure */, helper_->context()->AsWeakPtr(),
            &remote_endpoints_.back());
    ServiceWorkerProviderHost* host_raw = host.get();
    host->SetDocumentUrl(document_url);
    context_->AddProviderHost(std::move(host));
    return host_raw;
  }

  blink::mojom::ServiceWorkerErrorType Register(
      mojom::ServiceWorkerContainerHost* container_host,
      GURL pattern,
      GURL worker_url) {
    blink::mojom::ServiceWorkerErrorType error =
        blink::mojom::ServiceWorkerErrorType::kUnknown;
    auto options = blink::mojom::ServiceWorkerRegistrationOptions::New();
    options->scope = pattern;
    container_host->Register(
        worker_url, std::move(options),
        base::BindOnce([](blink::mojom::ServiceWorkerErrorType* out_error,
                          blink::mojom::ServiceWorkerErrorType error,
                          const base::Optional<std::string>& error_msg,
                          blink::mojom::ServiceWorkerRegistrationObjectInfoPtr
                              registration) { *out_error = error; },
                       &error));
    base::RunLoop().RunUntilIdle();
    return error;
  }

  blink::mojom::ServiceWorkerErrorType GetRegistration(
      mojom::ServiceWorkerContainerHost* container_host,
      GURL document_url,
      blink::mojom::ServiceWorkerRegistrationObjectInfoPtr* out_info =
          nullptr) {
    blink::mojom::ServiceWorkerErrorType error =
        blink::mojom::ServiceWorkerErrorType::kUnknown;
    container_host->GetRegistration(
        document_url,
        base::BindOnce(
            [](blink::mojom::ServiceWorkerErrorType* out_error,
               blink::mojom::ServiceWorkerRegistrationObjectInfoPtr* out_info,
               blink::mojom::ServiceWorkerErrorType error,
               const base::Optional<std::string>& error_msg,
               blink::mojom::ServiceWorkerRegistrationObjectInfoPtr
                   registration) {
              *out_error = error;
              if (out_info)
                *out_info = std::move(registration);
            },
            &error, out_info));
    base::RunLoop().RunUntilIdle();
    return error;
  }

  blink::mojom::ServiceWorkerErrorType GetRegistrations(
      mojom::ServiceWorkerContainerHost* container_host) {
    blink::mojom::ServiceWorkerErrorType error =
        blink::mojom::ServiceWorkerErrorType::kUnknown;
    container_host->GetRegistrations(base::BindOnce(
        [](blink::mojom::ServiceWorkerErrorType* out_error,
           blink::mojom::ServiceWorkerErrorType error,
           const base::Optional<std::string>& error_msg,
           base::Optional<std::vector<
               blink::mojom::ServiceWorkerRegistrationObjectInfoPtr>> infos) {
          *out_error = error;
        },
        &error));
    base::RunLoop().RunUntilIdle();
    return error;
  }

  void OnMojoError(const std::string& error) { bad_messages_.push_back(error); }

  std::vector<std::string> bad_messages_;
  TestBrowserThreadBundle thread_bundle_;
  std::unique_ptr<EmbeddedWorkerTestHelper> helper_;
  ServiceWorkerContextCore* context_;
  scoped_refptr<ServiceWorkerRegistration> registration1_;
  scoped_refptr<ServiceWorkerRegistration> registration2_;
  scoped_refptr<ServiceWorkerRegistration> registration3_;
  GURL script_url_;
  ServiceWorkerTestContentClient test_content_client_;
  TestContentBrowserClient test_content_browser_client_;
  ContentBrowserClient* old_content_browser_client_;
  int next_renderer_provided_id_;
  std::vector<ServiceWorkerRemoteProviderEndpoint> remote_endpoints_;

 private:
  ServiceWorkerProviderHost* CreateProviderHostInternal(
      const GURL& document_url,
      const GURL& topmost_frame_url,
      ServiceWorkerRemoteProviderEndpoint* remote_endpoint) {
    std::unique_ptr<ServiceWorkerProviderHost> host =
        ServiceWorkerProviderHost::PreCreateNavigationHost(
            helper_->context()->AsWeakPtr(), true,
            base::Callback<WebContents*(void)>());
    ServiceWorkerProviderHostInfo info(
        host->provider_id(), 1 /* route_id */,
        blink::mojom::ServiceWorkerProviderType::kForWindow,
        true /* is_parent_frame_secure */);
    remote_endpoint->BindWithProviderHostInfo(&info);
    host->CompleteNavigationInitialized(
        helper_->mock_render_process_id(), std::move(info),
        helper_->GetDispatcherHostForProcess(helper_->mock_render_process_id())
            ->AsWeakPtr());

    host->SetDocumentUrl(document_url);
    host->SetTopmostFrameUrl(topmost_frame_url);
    ServiceWorkerProviderHost* host_raw = host.get();
    context_->AddProviderHost(std::move(host));
    return host_raw;
  }

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerProviderHostTest);
};

TEST_F(ServiceWorkerProviderHostTest, MatchRegistration) {
  ServiceWorkerProviderHost* provider_host1 =
      CreateProviderHost(GURL("https://www.example.com/example1.html"));

  // Match registration should return the longest matching one.
  ASSERT_EQ(registration2_, provider_host1->MatchRegistration());
  provider_host1->RemoveMatchingRegistration(registration2_.get());
  ASSERT_EQ(registration1_, provider_host1->MatchRegistration());

  // Should return nullptr after removing all matching registrations.
  provider_host1->RemoveMatchingRegistration(registration1_.get());
  ASSERT_EQ(nullptr, provider_host1->MatchRegistration());

  // SetDocumentUrl sets all of matching registrations
  provider_host1->SetDocumentUrl(GURL("https://www.example.com/example1"));
  ASSERT_EQ(registration2_, provider_host1->MatchRegistration());
  provider_host1->RemoveMatchingRegistration(registration2_.get());
  ASSERT_EQ(registration1_, provider_host1->MatchRegistration());

  // SetDocumentUrl with another origin also updates matching registrations
  provider_host1->SetDocumentUrl(GURL("https://other.example.com/example"));
  ASSERT_EQ(registration3_, provider_host1->MatchRegistration());
  provider_host1->RemoveMatchingRegistration(registration3_.get());
  ASSERT_EQ(nullptr, provider_host1->MatchRegistration());
}

TEST_F(ServiceWorkerProviderHostTest, ContextSecurity) {
  ServiceWorkerProviderHost* provider_host_secure_parent =
      CreateProviderHost(GURL("https://www.example.com/example1.html"));
  ServiceWorkerProviderHost* provider_host_insecure_parent =
      CreateProviderHostWithInsecureParentFrame(
          GURL("https://www.example.com/example1.html"));

  // Insecure document URL.
  provider_host_secure_parent->SetDocumentUrl(GURL("http://host"));
  EXPECT_FALSE(provider_host_secure_parent->IsContextSecureForServiceWorker());

  // Insecure parent frame.
  provider_host_insecure_parent->SetDocumentUrl(GURL("https://host"));
  EXPECT_FALSE(
      provider_host_insecure_parent->IsContextSecureForServiceWorker());

  // Secure URL and parent frame.
  provider_host_secure_parent->SetDocumentUrl(GURL("https://host"));
  EXPECT_TRUE(provider_host_secure_parent->IsContextSecureForServiceWorker());

  // Exceptional service worker scheme.
  GURL url(std::string(kServiceWorkerScheme) + "://host");
  EXPECT_TRUE(url.is_valid());
  EXPECT_FALSE(IsOriginSecure(url));
  EXPECT_TRUE(OriginCanAccessServiceWorkers(url));
  provider_host_secure_parent->SetDocumentUrl(url);
  EXPECT_TRUE(provider_host_secure_parent->IsContextSecureForServiceWorker());

  // Exceptional service worker scheme with insecure parent frame.
  provider_host_insecure_parent->SetDocumentUrl(url);
  EXPECT_FALSE(
      provider_host_insecure_parent->IsContextSecureForServiceWorker());
}

class MockServiceWorkerRegistration : public ServiceWorkerRegistration {
 public:
  MockServiceWorkerRegistration(
      const blink::mojom::ServiceWorkerRegistrationOptions& options,
      int64_t registration_id,
      base::WeakPtr<ServiceWorkerContextCore> context)
      : ServiceWorkerRegistration(options, registration_id, context) {}

  void AddListener(ServiceWorkerRegistration::Listener* listener) override {
    listeners_.insert(listener);
  }

  void RemoveListener(ServiceWorkerRegistration::Listener* listener) override {
    listeners_.erase(listener);
  }

  const std::set<ServiceWorkerRegistration::Listener*>& listeners() {
    return listeners_;
  }

 protected:
  ~MockServiceWorkerRegistration() override{};

 private:
  std::set<ServiceWorkerRegistration::Listener*> listeners_;
};

TEST_F(ServiceWorkerProviderHostTest, RemoveProvider) {
  // Create a provider host connected with the renderer process.
  ServiceWorkerProviderHost* provider_host =
      CreateProviderHost(GURL("https://www.example.com/example1.html"));
  int process_id = provider_host->process_id();
  int provider_id = provider_host->provider_id();
  EXPECT_TRUE(context_->GetProviderHost(process_id, provider_id));

  // Disconnect the mojo pipe from the renderer side.
  ASSERT_TRUE(remote_endpoints_.back().host_ptr()->is_bound());
  remote_endpoints_.back().host_ptr()->reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(context_->GetProviderHost(process_id, provider_id));
}

class MockServiceWorkerContainer : public mojom::ServiceWorkerContainer {
 public:
  explicit MockServiceWorkerContainer(
      mojom::ServiceWorkerContainerAssociatedRequest request)
      : binding_(this, std::move(request)) {}

  ~MockServiceWorkerContainer() override = default;

  void SetController(mojom::ControllerServiceWorkerInfoPtr controller_info,
                     const std::vector<blink::mojom::WebFeature>& used_features,
                     bool should_notify_controllerchange) override {
    was_set_controller_called_ = true;
  }
  void PostMessageToClient(blink::mojom::ServiceWorkerObjectInfoPtr controller,
                           blink::TransferableMessage message) override {}
  void CountFeature(blink::mojom::WebFeature feature) override {}

  bool was_set_controller_called() const { return was_set_controller_called_; }

 private:
  bool was_set_controller_called_ = false;
  mojo::AssociatedBinding<mojom::ServiceWorkerContainer> binding_;
};

TEST_F(ServiceWorkerProviderHostTest, Controller) {
  // Create a host.
  std::unique_ptr<ServiceWorkerProviderHost> host =
      ServiceWorkerProviderHost::PreCreateNavigationHost(
          helper_->context()->AsWeakPtr(), true /* are_ancestors_secure */,
          base::Callback<WebContents*(void)>());
  ServiceWorkerProviderHostInfo info(
      host->provider_id(), 1 /* route_id */,
      blink::mojom::ServiceWorkerProviderType::kForWindow,
      true /* is_parent_frame_secure */);
  remote_endpoints_.emplace_back();
  remote_endpoints_.back().BindWithProviderHostInfo(&info);
  auto container = std::make_unique<MockServiceWorkerContainer>(
      std::move(*remote_endpoints_.back().client_request()));

  // Create an active version and then start the navigation.
  scoped_refptr<ServiceWorkerVersion> version = new ServiceWorkerVersion(
      registration1_.get(), GURL("https://www.example.com/sw.js"),
      1 /* version_id */, helper_->context()->AsWeakPtr());
  registration1_->SetActiveVersion(version);

  // Finish the navigation.
  host->SetDocumentUrl(GURL("https://www.example.com/page"));
  host->CompleteNavigationInitialized(
      helper_->mock_render_process_id(), std::move(info),
      helper_->GetDispatcherHostForProcess(helper_->mock_render_process_id())
          ->AsWeakPtr());

  host->AssociateRegistration(registration1_.get(),
                              false /* notify_controllerchange */);
  base::RunLoop().RunUntilIdle();

  // The page should be controlled since there was an active version at the
  // time navigation started. The SetController IPC should have been sent.
  EXPECT_TRUE(host->active_version());
  EXPECT_EQ(host->active_version(), host->controller());
  EXPECT_TRUE(container->was_set_controller_called());
}

TEST_F(ServiceWorkerProviderHostTest, ActiveIsNotController) {
  // Create a host.
  std::unique_ptr<ServiceWorkerProviderHost> host =
      ServiceWorkerProviderHost::PreCreateNavigationHost(
          helper_->context()->AsWeakPtr(), true /* are_ancestors_secure */,
          base::Callback<WebContents*(void)>());
  ServiceWorkerProviderHostInfo info(
      host->provider_id(), 1 /* route_id */,
      blink::mojom::ServiceWorkerProviderType::kForWindow,
      true /* is_parent_frame_secure */);
  remote_endpoints_.emplace_back();
  remote_endpoints_.back().BindWithProviderHostInfo(&info);
  auto container = std::make_unique<MockServiceWorkerContainer>(
      std::move(*remote_endpoints_.back().client_request()));

  // Create an installing version and then start the navigation.
  scoped_refptr<ServiceWorkerVersion> version = new ServiceWorkerVersion(
      registration1_.get(), GURL("https://www.example.com/sw.js"),
      1 /* version_id */, helper_->context()->AsWeakPtr());
  registration1_->SetInstallingVersion(version);


  // Finish the navigation.
  host->SetDocumentUrl(GURL("https://www.example.com/page"));
  host->CompleteNavigationInitialized(
      helper_->mock_render_process_id(), std::move(info),
      helper_->GetDispatcherHostForProcess(helper_->mock_render_process_id())
          ->AsWeakPtr());

  host->AssociateRegistration(registration1_.get(),
                              false /* notify_controllerchange */);
  // Promote the worker to active while navigation is still happening.
  registration1_->SetActiveVersion(version);
  base::RunLoop().RunUntilIdle();

  // The page should not be controlled since there was no active version at the
  // time navigation started. Furthermore, no SetController IPC should have been
  // sent.
  EXPECT_TRUE(host->active_version());
  EXPECT_FALSE(host->controller());
  EXPECT_FALSE(container->was_set_controller_called());
}

TEST_F(ServiceWorkerProviderHostTest,
       Register_ContentSettingsDisallowsServiceWorker) {
  ServiceWorkerTestContentBrowserClient test_browser_client;
  ContentBrowserClient* old_browser_client =
      SetBrowserClientForTesting(&test_browser_client);

  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHostWithTopmostFrameUrl(
          GURL("https://www.example.com/foo"),
          GURL("https://www.example.com/top"));

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kDisabled,
            Register(remote_endpoint.host_ptr()->get(),
                     GURL("https://www.example.com/scope"),
                     GURL("https://www.example.com/bar")));
  ASSERT_EQ(1ul, test_browser_client.logs().size());
  EXPECT_EQ(GURL("https://www.example.com/scope"),
            test_browser_client.logs()[0].scope);
  EXPECT_EQ(GURL("https://www.example.com/top"),
            test_browser_client.logs()[0].first_party);

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kDisabled,
            GetRegistration(remote_endpoint.host_ptr()->get(),
                            GURL("https://www.example.com/")));
  ASSERT_EQ(2ul, test_browser_client.logs().size());
  EXPECT_EQ(GURL("https://www.example.com/foo"),
            test_browser_client.logs()[1].scope);
  EXPECT_EQ(GURL("https://www.example.com/top"),
            test_browser_client.logs()[1].first_party);

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kDisabled,
            GetRegistrations(remote_endpoint.host_ptr()->get()));
  ASSERT_EQ(3ul, test_browser_client.logs().size());
  EXPECT_EQ(GURL("https://www.example.com/foo"),
            test_browser_client.logs()[2].scope);
  EXPECT_EQ(GURL("https://www.example.com/top"),
            test_browser_client.logs()[2].first_party);

  SetBrowserClientForTesting(old_browser_client);
}

TEST_F(ServiceWorkerProviderHostTest, AllowsServiceWorker) {
  // Create an active version.
  scoped_refptr<ServiceWorkerVersion> version =
      base::MakeRefCounted<ServiceWorkerVersion>(
          registration1_.get(), GURL("https://www.example.com/sw.js"),
          1 /* version_id */, helper_->context()->AsWeakPtr());
  registration1_->SetActiveVersion(version);

  ServiceWorkerRemoteProviderEndpoint remote_endpoint;
  std::unique_ptr<ServiceWorkerProviderHost> host =
      CreateProviderHostForServiceWorkerContext(
          helper_->mock_render_process_id(), true /* is_parent_frame_secure */,
          version.get(), helper_->context()->AsWeakPtr(), &remote_endpoint);

  ServiceWorkerTestContentBrowserClient test_browser_client;
  ContentBrowserClient* old_browser_client =
      SetBrowserClientForTesting(&test_browser_client);

  EXPECT_FALSE(host->AllowServiceWorker(GURL("https://www.example.com/scope")));

  ASSERT_EQ(1ul, test_browser_client.logs().size());
  EXPECT_EQ(GURL("https://www.example.com/scope"),
            test_browser_client.logs()[0].scope);
  EXPECT_EQ(GURL("https://www.example.com/sw.js"),
            test_browser_client.logs()[0].first_party);

  SetBrowserClientForTesting(old_browser_client);
}

TEST_F(ServiceWorkerProviderHostTest, Register_HTTPS) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            Register(remote_endpoint.host_ptr()->get(),
                     GURL("https://www.example.com/"),
                     GURL("https://www.example.com/bar")));
}

TEST_F(ServiceWorkerProviderHostTest, Register_NonSecureTransportLocalhost) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("http://127.0.0.3:81/foo"));

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            Register(remote_endpoint.host_ptr()->get(),
                     GURL("http://127.0.0.3:81/bar"),
                     GURL("http://127.0.0.3:81/baz")));
}

TEST_F(ServiceWorkerProviderHostTest, Register_InvalidScopeShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(), GURL(""),
           GURL("https://www.example.com/bar/hoge.js"));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, Register_InvalidScriptShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/bar/"), GURL(""));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, Register_NonSecureOriginShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("http://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(), GURL("http://www.example.com/"),
           GURL("http://www.example.com/bar"));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, Register_CrossOriginShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  // Script has a different host
  Register(remote_endpoint.host_ptr()->get(), GURL("https://www.example.com/"),
           GURL("https://foo.example.com/bar"));
  EXPECT_EQ(1u, bad_messages_.size());

  // Scope has a different host
  Register(remote_endpoint.host_ptr()->get(), GURL("https://foo.example.com/"),
           GURL("https://www.example.com/bar"));
  EXPECT_EQ(2u, bad_messages_.size());

  // Script has a different port
  Register(remote_endpoint.host_ptr()->get(), GURL("https://www.example.com/"),
           GURL("https://www.example.com:8080/bar"));
  EXPECT_EQ(3u, bad_messages_.size());

  // Scope has a different transport
  Register(remote_endpoint.host_ptr()->get(), GURL("wss://www.example.com/"),
           GURL("https://www.example.com/bar"));
  EXPECT_EQ(4u, bad_messages_.size());

  // Script and scope have a different host but match each other
  Register(remote_endpoint.host_ptr()->get(), GURL("https://foo.example.com/"),
           GURL("https://foo.example.com/bar"));
  EXPECT_EQ(5u, bad_messages_.size());

  // Script and scope URLs are invalid
  Register(remote_endpoint.host_ptr()->get(), GURL(), GURL("h@ttps://@"));
  EXPECT_EQ(6u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, Register_BadCharactersShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/%2f"),
           GURL("https://www.example.com/"));
  EXPECT_EQ(1u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/%2F"),
           GURL("https://www.example.com/"));
  EXPECT_EQ(2u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(), GURL("https://www.example.com/"),
           GURL("https://www.example.com/%2f"));
  EXPECT_EQ(3u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/%5c"),
           GURL("https://www.example.com/"));
  EXPECT_EQ(4u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(), GURL("https://www.example.com/"),
           GURL("https://www.example.com/%5c"));
  EXPECT_EQ(5u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(), GURL("https://www.example.com/"),
           GURL("https://www.example.com/%5C"));
  EXPECT_EQ(6u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, Register_FileSystemDocumentShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(
          GURL("filesystem:https://www.example.com/temporary/a"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(),
           GURL("filesystem:https://www.example.com/temporary/"),
           GURL("https://www.example.com/temporary/bar"));
  EXPECT_EQ(1u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/temporary/"),
           GURL("filesystem:https://www.example.com/temporary/bar"));
  EXPECT_EQ(2u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("filesystem:https://www.example.com/temporary/"),
           GURL("filesystem:https://www.example.com/temporary/bar"));
  EXPECT_EQ(3u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest,
       Register_FileSystemScriptOrScopeShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(
          GURL("https://www.example.com/temporary/"));

  ASSERT_TRUE(bad_messages_.empty());
  Register(remote_endpoint.host_ptr()->get(),
           GURL("filesystem:https://www.example.com/temporary/"),
           GURL("https://www.example.com/temporary/bar"));
  EXPECT_EQ(1u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("https://www.example.com/temporary/"),
           GURL("filesystem:https://www.example.com/temporary/bar"));
  EXPECT_EQ(2u, bad_messages_.size());

  Register(remote_endpoint.host_ptr()->get(),
           GURL("filesystem:https://www.example.com/temporary/"),
           GURL("filesystem:https://www.example.com/temporary/bar"));
  EXPECT_EQ(3u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, EarlyContextDeletion) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  helper_->ShutdownContext();

  // Let the shutdown reach the simulated IO thread.
  base::RunLoop().RunUntilIdle();

  // Because ServiceWorkerContextCore owns ServiceWorkerProviderHost, our
  // ServiceWorkerProviderHost instance has destroyed.
  EXPECT_TRUE(remote_endpoint.host_ptr()->encountered_error());
}

TEST_F(ServiceWorkerProviderHostTest, GetRegistration_Success) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  const GURL kScope("https://www.example.com/");
  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            Register(remote_endpoint.host_ptr()->get(), kScope,
                     GURL("https://www.example.com/sw.js")));
  blink::mojom::ServiceWorkerRegistrationObjectInfoPtr info;
  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            GetRegistration(remote_endpoint.host_ptr()->get(), kScope, &info));
  ASSERT_TRUE(info);
  EXPECT_EQ(kScope, info->options->scope);
}

TEST_F(ServiceWorkerProviderHostTest,
       GetRegistration_NotFoundShouldReturnNull) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  blink::mojom::ServiceWorkerRegistrationObjectInfoPtr info;
  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            GetRegistration(remote_endpoint.host_ptr()->get(),
                            GURL("https://www.example.com/"), &info));
  EXPECT_FALSE(info);
}

TEST_F(ServiceWorkerProviderHostTest, GetRegistration_CrossOriginShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  GetRegistration(remote_endpoint.host_ptr()->get(),
                  GURL("https://foo.example.com/"));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, GetRegistration_InvalidScopeShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  GetRegistration(remote_endpoint.host_ptr()->get(), GURL(""));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest,
       GetRegistration_NonSecureOriginShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("http://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  GetRegistration(remote_endpoint.host_ptr()->get(),
                  GURL("http://www.example.com/"));
  EXPECT_EQ(1u, bad_messages_.size());
}

TEST_F(ServiceWorkerProviderHostTest, GetRegistrations_SecureOrigin) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("https://www.example.com/foo"));

  EXPECT_EQ(blink::mojom::ServiceWorkerErrorType::kNone,
            GetRegistrations(remote_endpoint.host_ptr()->get()));
}

TEST_F(ServiceWorkerProviderHostTest,
       GetRegistrations_NonSecureOriginShouldFail) {
  ServiceWorkerRemoteProviderEndpoint remote_endpoint =
      PrepareServiceWorkerProviderHost(GURL("http://www.example.com/foo"));

  ASSERT_TRUE(bad_messages_.empty());
  GetRegistrations(remote_endpoint.host_ptr()->get());
  EXPECT_EQ(1u, bad_messages_.size());
}

}  // namespace content
