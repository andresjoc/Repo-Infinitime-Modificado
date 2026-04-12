#pragma once
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <atomic>
#undef max
#undef min

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
    class NimbleController;

    class RawPpgService {
    public:
      RawPpgService(NimbleController& nimble, Controllers::HeartRateController& heartRateController);
      void Init();
      int OnRawPpgRequested(uint16_t attributeHandle, ble_gatt_access_ctxt* context);
      void OnNewRawPpgValue(uint32_t hrs, uint32_t als);

      void SubscribeNotification(uint16_t attributeHandle);
      void UnsubscribeNotification(uint16_t attributeHandle);

    private:
      NimbleController& nimble;
      Controllers::HeartRateController& heartRateController;

      // b5f90000-8456-4c84-a6b9-5f95a2f2f1f0
      static constexpr ble_uuid128_t rawPpgServiceUuid {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0xf0, 0xf1, 0xf2, 0xa2, 0x95, 0x5f, 0xb9, 0xa6, 0x84, 0x4c, 0x56, 0x84, 0x00, 0x00, 0xf9, 0xb5},
      };

      // b5f90001-8456-4c84-a6b9-5f95a2f2f1f0
      static constexpr ble_uuid128_t rawPpgMeasurementUuid {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0xf0, 0xf1, 0xf2, 0xa2, 0x95, 0x5f, 0xb9, 0xa6, 0x84, 0x4c, 0x56, 0x84, 0x01, 0x00, 0xf9, 0xb5},
      };

      struct ble_gatt_chr_def characteristicDefinition[2];
      struct ble_gatt_svc_def serviceDefinition[2];

      uint16_t rawPpgMeasurementHandle;
      uint32_t lastHrs = 0;
      uint32_t lastAls = 0;
      uint32_t sampleCounter = 0;
      std::atomic_bool rawPpgMeasurementNotificationEnabled {false};
    };
  }
}
