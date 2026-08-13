//-*-C++-*-

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <lcms2.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

namespace pdflib::icc
{
  // A reusable embedded-profile -> sRGB transform.
  //
  // Colour operators are issued per drawing op -- a single slide can carry
  // hundreds of scn against one space -- so building a transform per call is
  // not viable. The handle is shared and freed once, which also makes the
  // owning colour-space resource safe to copy.
  inline std::shared_ptr<void> make_transform_to_srgb(
    std::vector<uint8_t> const& profile_bytes,
    int                         components)
  {
    if(profile_bytes.empty() or components <= 0)
      {
        return nullptr;
      }

    cmsUInt32Number input_type = 0;
    switch(components)
      {
        case 1: input_type = TYPE_GRAY_8; break;
        case 3: input_type = TYPE_RGB_8;  break;
        case 4: input_type = TYPE_CMYK_8; break;
        default: return nullptr;
      }

    cmsHPROFILE input_profile = cmsOpenProfileFromMem(
      profile_bytes.data(), static_cast<cmsUInt32Number>(profile_bytes.size()));
    if(not input_profile)
      {
        LOG_S(WARNING) << "icc: failed to open embedded profile for colour transform";
        return nullptr;
      }

    cmsHPROFILE output_profile = cmsCreate_sRGBProfile();
    if(not output_profile)
      {
        cmsCloseProfile(input_profile);
        return nullptr;
      }

    // Perceptual matches the rendering intent these documents ask for and is
    // what viewers use for pictorial content.
    cmsHTRANSFORM transform = cmsCreateTransform(
      input_profile, input_type, output_profile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);

    cmsCloseProfile(input_profile);
    cmsCloseProfile(output_profile);

    if(not transform)
      {
        LOG_S(WARNING) << "icc: failed to build colour transform to sRGB";
        return nullptr;
      }

    return std::shared_ptr<void>(transform,
                                 [](void* t) { if(t) { cmsDeleteTransform(t); } });
  }

  // Maps one colour, components given in PDF's 0..1 range, through `transform`.
  inline bool transform_color_to_rgb(std::shared_ptr<void> const& transform,
                                     std::vector<double> const&   comps,
                                     std::array<int, 3>&          rgb)
  {
    if(not transform or comps.empty() or comps.size() > 4)
      {
        return false;
      }

    uint8_t in[4] = {0, 0, 0, 0};
    for(std::size_t i = 0; i < comps.size(); ++i)
      {
        const double v = comps[i] < 0.0 ? 0.0 : (comps[i] > 1.0 ? 1.0 : comps[i]);
        in[i] = static_cast<uint8_t>(std::lround(v * 255.0));
      }

    uint8_t out[3] = {0, 0, 0};
    cmsDoTransform(transform.get(), in, out, 1);

    rgb = {static_cast<int>(out[0]), static_cast<int>(out[1]), static_cast<int>(out[2])};
    return true;
  }

  inline std::vector<uint8_t> transform_palette_to_rgb(
    std::vector<uint8_t> const& palette,
    int                         components,
    std::vector<uint8_t> const& profile_bytes)
  {
    if(profile_bytes.empty() or palette.empty() or components <= 0)
      {
        return {};
      }

    if((palette.size() % static_cast<std::size_t>(components)) != 0u)
      {
        LOG_S(WARNING) << "icc: palette size is not divisible by component count";
        return {};
      }

    cmsUInt32Number input_type = 0;
    switch(components)
      {
        case 1: input_type = TYPE_GRAY_8; break;
        case 3: input_type = TYPE_RGB_8; break;
        case 4: input_type = TYPE_CMYK_8; break;
        default:
          LOG_S(WARNING) << "icc: unsupported palette component count " << components;
          return {};
      }

    cmsHPROFILE input_profile = cmsOpenProfileFromMem(
      profile_bytes.data(), static_cast<cmsUInt32Number>(profile_bytes.size()));
    if(not input_profile)
      {
        LOG_S(WARNING) << "icc: failed to open embedded ICC profile";
        return {};
      }

    cmsHPROFILE output_profile = cmsCreate_sRGBProfile();
    if(not output_profile)
      {
        cmsCloseProfile(input_profile);
        LOG_S(WARNING) << "icc: failed to create sRGB profile";
        return {};
      }

    cmsHTRANSFORM transform = cmsCreateTransform(input_profile,
                                                 input_type,
                                                 output_profile,
                                                 TYPE_RGB_8,
                                                 INTENT_RELATIVE_COLORIMETRIC,
                                                 0);
    if(not transform)
      {
        cmsCloseProfile(output_profile);
        cmsCloseProfile(input_profile);
        LOG_S(WARNING) << "icc: failed to create ICC transform";
        return {};
      }

    const cmsUInt32Number entry_count =
      static_cast<cmsUInt32Number>(palette.size() / static_cast<std::size_t>(components));
    std::vector<uint8_t> rgb(static_cast<std::size_t>(entry_count) * 3u, 0u);
    cmsDoTransform(transform, palette.data(), rgb.data(), entry_count);

    cmsDeleteTransform(transform);
    cmsCloseProfile(output_profile);
    cmsCloseProfile(input_profile);
    return rgb;
  }
}
