// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2019 Leap Motion Corporation. All Rights Reserved.

#include "drivers.h"
#include <librealuvc/ru_uvc.h>
#include <librealuvc/ru_videocapture.h>
#include <cstdio>
#include "leap_xu.h"

#if 1
#define D(...) { printf("DEBUG[%s,%d] ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); }
#else
#define D(...) { }
#endif

namespace librealuvc {

using std::shared_ptr;

static int32_t saturate(double val, int32_t min, int32_t max) {
  int32_t ival = (int32_t)val;
  if (ival < min) return min;
  if (ival > max) return max;
  return ival;
}

static int32_t flag(double val) {
  return ((val == 0.0) ? 0 : 1);
}

static const int CY_FX_UVC_PU_GAIN_SELECTOR_GAIN  = 0x4000;
static const int CY_FX_UVC_PU_GAIN_SELECTOR_FOCUS = 0x8000;
static const int CY_FX_UVC_PU_GAIN_SELECTOR_WHITE = 0xc000;

class PropertyDriverRigel : public IPropertyDriver {
 private:
  shared_ptr<uvc_device> dev_;
  extension_unit leap_xu_;
  double leds_;
 public:
  PropertyDriverRigel(const shared_ptr<uvc_device>& dev) :
    dev_(dev),
    leds_(0) {
    D("PropertyDriverRigel::ctor() ...");
    leap_xu_.subdevice = 0;
    leap_xu_.unit = 1;
    leap_xu_.node = 4;
    leap_xu_.id = guid LEAP_XU_GUID;
    try {
      dev_->init_xu(leap_xu_);
    } catch (std::exception& e) {
      printf("EXCEPTION: init_xu: %s\n", e.what());
    }
  }

  int get_frame_fixup() override {
    return 2;
  }
  
  HandlerResult get_prop(int prop_id, double* val) override {
    bool ok = true;
    int32_t ival = 0;
    switch (prop_id) {
      case cv::CAP_PROP_EXPOSURE: {
        uint16_t tmp = 0;
        ok = dev_->get_xu(leap_xu_, LEAP_XU_EXPOSURE_CONTROL, (uint8_t*)&tmp, sizeof(tmp));
        *val = (double)tmp;
        break;
      }
      case cv::CAP_PROP_GAIN:
        ok = dev_->get_pu(RU_OPTION_GAIN, ival);
        *val = (double)ival;
        break;
      case cv::CAP_PROP_GAMMA:
        *val = 0.0;
        break;
      case CAP_PROP_LEAP_HDR:
        *val = 0.0;
        break;
      case CAP_PROP_LEAP_LEDS:
        *val = leds_;
        break;
      default:
        return kNotHandled;
    }
    return (ok ? kHandlerTrue : kHandlerFalse);
  }
  
  HandlerResult get_prop_range(int prop_id, double* min_val, double* max_val) override {
    bool ok = true;
    *min_val = 0;
    *max_val = 0;
    switch (prop_id) {
      case cv::CAP_PROP_EXPOSURE:
        *max_val = 1000;
        break;
      case cv::CAP_PROP_GAIN:
        *min_val = 16;
        *max_val = 500;
        break;
      case cv::CAP_PROP_GAMMA:
        ok = false;
        break;
      case CAP_PROP_LEAP_HDR:
        ok = false;
        break;
      case CAP_PROP_LEAP_LEDS:
        *max_val = 1;
        break;
      default:
        return kNotHandled;
    }
    return (ok ? kHandlerTrue : kHandlerFalse);
  }
  
  HandlerResult set_prop(int prop_id, double val) override {
    bool ok = true;
    int32_t ival;
    switch (prop_id) {
      case cv::CAP_PROP_EXPOSURE: {
        // Exposure goes through set_xu
        uint16_t tmp = (uint16_t)saturate(val, 10, 0xffff);
        ok = dev_->set_xu(leap_xu_, LEAP_XU_EXPOSURE_CONTROL, (uint8_t*)&tmp, sizeof(tmp));
        break;
      }
      case cv::CAP_PROP_GAIN: {
        ival = (int)val;
        int32_t code;
        if      (ival <  16) code = 0x00;
        else if (ival <  32) code = 0x00 + (ival-16);
        else if (ival <  63) code = 0x10 + ((ival- 31)>>1);
        else if (ival < 126) code = 0x20 + ((ival- 62)>>2);
        else if (ival < 252) code = 0x30 + ((ival-124)>>3);
        else                 code = 0x40 + ((ival-248)>>4);
        D("set_pu(RU_OPTION_GAIN, 0x%04x) ...", (int)code);
        ok = dev_->set_pu(RU_OPTION_GAIN, code);
        break;
      }
      case cv::CAP_PROP_GAMMA:
        // No hardware gamma
        ok = ((val == 0.0) ? true : false);
        break;
      case CAP_PROP_LEAP_HDR:
        // No hardware HDR
        ok = ((val == 0.0) ? true : false);
        break;
      case CAP_PROP_LEAP_LEDS:
        if (leds_ != val) {
          // LED control goes through set_xu
          uint8_t tmpA = flag(val);
          ok = dev_->set_xu(leap_xu_, LEAP_XU_STROBE_CONTROL, (uint8_t*)&tmpA, sizeof(tmpA));
          uint32_t tmpB = flag(val);
          ok &= dev_->set_xu(leap_xu_, LEAP_XU_ESC_LED_CHARGE, (uint8_t*)&tmpB, sizeof(tmpB));
          if (ok) leds_ = val;
        }
        break;
      default:
        return kNotHandled;
    }
    return (ok ? kHandlerTrue : kHandlerFalse);
  }
};

#define VENDOR_RIGEL  0x2936
#define PRODUCT_RIGEL 0x1202

LIBREALUVC_EXPORT void import_driver_rigel() {
  register_property_driver(
    VENDOR_RIGEL,
    PRODUCT_RIGEL,
    [](const shared_ptr<uvc_device>& realuvc)->shared_ptr<IPropertyDriver> {
      return std::make_shared<PropertyDriverRigel>(realuvc);
    }
  );
}

} // end librealuvc