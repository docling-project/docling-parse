//-*-C++-*-

#pragma once

#include <cstdint>
#include <vector>

#include <lcms2.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

namespace pdflib::icc
{
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
