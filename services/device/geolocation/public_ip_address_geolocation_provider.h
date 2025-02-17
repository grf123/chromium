// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_GEOLOCATION_PUBLIC_IP_ADDRESS_GEOLOCATION_PROVIDER_H_
#define SERVICES_DEVICE_GEOLOCATION_PUBLIC_IP_ADDRESS_GEOLOCATION_PROVIDER_H_

#include <string>

#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/device/geolocation/public_ip_address_geolocator.h"
#include "services/device/geolocation/public_ip_address_location_notifier.h"
#include "services/device/public/interfaces/geolocation.mojom.h"
#include "services/device/public/interfaces/public_ip_address_geolocation_provider.mojom.h"

namespace device {

// Implementation of PublicIpAddressGeolocationProvider Mojo interface that will
// provide mojom::Geolocation implementations that use IP-only geolocation.
// Binds multiple PublicIpAddressGeolocationProvider requests.
//
// Sequencing:
// * Must be used and destroyed on the same sequence.
// * Provides mojom::Geolocation instances that are bound on the same sequence.
class PublicIpAddressGeolocationProvider
    : public mojom::PublicIpAddressGeolocationProvider {
 public:
  // Initialize PublicIpAddressGeolocationProvider using the specified Google
  // |api_key| and a URL request context produced by |request_context_producer|
  // for network location requests.
  PublicIpAddressGeolocationProvider(
      GeolocationProvider::RequestContextProducer request_context_producer,
      const std::string& api_key);
  ~PublicIpAddressGeolocationProvider() override;

  // Binds a PublicIpAddressGeolocationProvider request to this instance.
  void Bind(mojom::PublicIpAddressGeolocationProviderRequest request);

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // mojom::PublicIpAddressGeolocationProvider implementation:
  // Provides a Geolocation instance that performs IP-geolocation only.
  void CreateGeolocation(
      const net::MutablePartialNetworkTrafficAnnotationTag& tag,
      mojom::GeolocationRequest request) override;

  // Central PublicIpAddressLocationNotifier for use by all implementations of
  // mojom::Geolocation provided by the CreateGeolocation method.
  // Note that this must be before the StrongBindingSet<mojom::Geolocation> as
  // it must outlive the Geolocation implementations.
  std::unique_ptr<PublicIpAddressLocationNotifier>
      public_ip_address_location_notifier_;

  mojo::BindingSet<mojom::PublicIpAddressGeolocationProvider>
      provider_binding_set_;

  mojo::StrongBindingSet<mojom::Geolocation> geolocation_binding_set_;

  DISALLOW_COPY_AND_ASSIGN(PublicIpAddressGeolocationProvider);
};

}  // namespace device

#endif  // SERVICES_DEVICE_GEOLOCATION_PUBLIC_IP_ADDRESS_GEOLOCATION_PROVIDER_H_
