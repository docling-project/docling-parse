//-*-C++-*-

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pdflib::color
{
  // DeviceCMYK -> sRGB (ISO 32000-1, 8.6.4.4).
  //
  // DeviceCMYK has no colorimetric definition: the standard calls it
  // device-dependent and leaves the rendering to the consumer, so every reader
  // picks one. What a reader is expected to show is what a press would print,
  // and the textbook subtractive formula -- R = (1-C)(1-K) -- is not that. It
  // assumes perfect inks on perfect paper, so it renders solid cyan as #00FFFF
  // where a press gives #00AEEF, solid black as #000000 where a press gives
  // #231F20, and CMY at 100 % as pure black rather than the muddy #414042 that
  // actually comes off the sheet. Saturated artwork ends up looking like a
  // screen gamut instead of ink.
  //
  // What is implemented here is the standard printing model for the same
  // question -- Neugebauer with a Yule-Nielsen exponent (7.10-era colour
  // science, not PDF):
  //
  //   1. each ink's requested amount passes through a tone curve, giving the
  //      area it actually covers once dot gain is accounted for;
  //   2. the CMY result is the area-weighted mix of the eight overprint
  //      primaries (paper, C, M, C+M, Y, C+Y, M+Y, C+M+Y), combined with a
  //      per-channel Yule-Nielsen exponent that models light scattering in the
  //      paper;
  //   3. black ink multiplies that, as a neutral transmittance sampled at nine
  //      points along K.
  //
  // The parameters below were fitted so the model reproduces a SWOP-coated
  // press -- specifically, so its output tracks the DeviceCMYK rendering of
  // PDFium and Acrobat, which is what readers of these documents will have
  // seen. Over a 9x9x9x9 sweep it stays within a mean of 6.1 and a maximum of
  // 24 code values of that target, against 26.1 and 115 for the subtractive
  // formula, and it is exact on the neutral K-only axis, which is where most
  // CMYK ink on a page actually sits.
  //
  // This is a rendering, not a measurement. A document that says what its
  // colours mean -- an ICCBased space, or /DefaultCMYK -- should be honoured
  // through its profile instead of coming here.
  namespace detail
  {
    // The press model, resolved once on first use.
    struct cmyk_model
    {
      // The eight Neugebauer primaries in linear light, indexed by
      // (c ? 1 : 0) | (m ? 2 : 0) | (y ? 4 : 0). Cyan and cyan+yellow carry a
      // negative red coordinate on purpose: process inks fall outside the sRGB
      // gamut, and clipping the primaries into the cube here instead of at the
      // end would flatten the whole cyan ramp.
      std::array<std::array<double, 3>, 8> corner;

      // corner split for the Yule-Nielsen mix: the non-negative part enters
      // through the exponent, the negative part mixes linearly.
      std::array<std::array<double, 3>, 8> encoded_positive;
      std::array<std::array<double, 3>, 8> negative;

      std::array<double, 3> yule_nielsen;  // per output channel
      std::array<double, 3> ink_tone;      // dot gain of c, m, y

      // Transmittance of the black ink at k = 0, 1/8, ..., 1, in linear light.
      std::array<std::array<double, 3>, 9> black;
    };

    inline double srgb_to_linear(double v)
    {
      return (v <= 0.04045) ? (v / 12.92) : std::pow((v + 0.055) / 1.055, 2.4);
    }

    inline double linear_to_srgb(double v)
    {
      v = std::min(1.0, std::max(0.0, v));
      return (v <= 0.0031308) ? (12.92 * v)
                              : (1.055 * std::pow(v, 1.0 / 2.4) - 0.055);
    }

    inline const cmyk_model& get_cmyk_model()
    {
      static const cmyk_model model = []
        {
          cmyk_model m;

          m.yule_nielsen = {1.553623, 1.348579, 1.536033};
          m.ink_tone     = {1.176647, 1.212366, 1.145622};

          m.corner = {{
            {{ 1.000000,  1.000000,  1.000000}},  // paper
            {{-0.179803,  0.419937,  0.868289}},  // C
            {{ 0.869084, -0.001406,  0.268871}},  // M
            {{ 0.039807,  0.023510,  0.277328}},  // C + M
            {{ 1.052223,  0.853703,  0.007227}},  // Y
            {{-0.074325,  0.403406,  0.078689}},  // C + Y
            {{ 0.879638,  0.012292,  0.011953}},  // M + Y
            {{ 0.043065,  0.031199,  0.032550}},  // C + M + Y
          }};

          for(std::size_t i = 0; i < m.corner.size(); i++)
            {
              for(std::size_t j = 0; j < 3; j++)
                {
                  const double v = m.corner[i][j];
                  m.encoded_positive[i][j] =
                    std::pow(std::max(0.0, v), 1.0 / m.yule_nielsen[j]);
                  m.negative[i][j] = std::min(0.0, v);
                }
            }

          // Neutral ramp of the black ink, in 0-255 sRGB, at k = 0 .. 1.
          const std::array<std::array<double, 3>, 9> ramp = {{
            {{255, 255, 255}}, {{225, 226, 228}}, {{199, 200, 202}},
            {{173, 174, 178}}, {{147, 149, 152}}, {{123, 125, 128}},
            {{ 99,  99, 102}}, {{ 69,  70,  71}}, {{ 35,  31,  32}},
          }};

          for(std::size_t i = 0; i < ramp.size(); i++)
            {
              for(std::size_t j = 0; j < 3; j++)
                {
                  m.black[i][j] = srgb_to_linear(ramp[i][j] / 255.0);
                }
            }

          return m;
        }();

      return model;
    }
  }

  // Converts ink amounts in [0, 1] to sRGB in [0, 1]. Values outside the unit
  // interval are clamped, as 8.6.4.4 requires of the colour operands.
  inline void cmyk_to_rgb(double c, double m, double y, double k,
                          double& red, double& green, double& blue)
  {
    const detail::cmyk_model& model = detail::get_cmyk_model();

    auto clamp_01 = [](double v) { return std::min(1.0, std::max(0.0, v)); };

    // dot gain: the area an ink covers is not the amount that was asked for
    const double area[3] = {
      std::pow(clamp_01(c), 1.0 / model.ink_tone[0]),
      std::pow(clamp_01(m), 1.0 / model.ink_tone[1]),
      std::pow(clamp_01(y), 1.0 / model.ink_tone[2]),
    };

    // fraction of the sheet carrying each of the eight overprints
    double weight[8];
    for(std::size_t i = 0; i < 8; i++)
      {
        weight[i] = ((i & 1u) ? area[0] : 1.0 - area[0]) *
                    ((i & 2u) ? area[1] : 1.0 - area[1]) *
                    ((i & 4u) ? area[2] : 1.0 - area[2]);
      }

    // black ink transmittance, linear between the nine sampled points
    const double kx = clamp_01(k) * 8.0;
    const std::size_t k0 = std::min<std::size_t>(7, static_cast<std::size_t>(kx));
    const double kf = kx - static_cast<double>(k0);

    double out[3];
    for(std::size_t j = 0; j < 3; j++)
      {
        double mixed = 0.0;
        double offset = 0.0;
        for(std::size_t i = 0; i < 8; i++)
          {
            mixed += weight[i] * model.encoded_positive[i][j];
            offset += weight[i] * model.negative[i][j];
          }

        const double linear =
          std::pow(std::max(0.0, mixed), model.yule_nielsen[j]) + offset;

        const double transmittance =
          model.black[k0][j] * (1.0 - kf) + model.black[k0 + 1][j] * kf;

        out[j] = detail::linear_to_srgb(linear * transmittance);
      }

    red = out[0];
    green = out[1];
    blue = out[2];
  }

  // 0-255 form, for callers that hold colours as bytes.
  inline std::array<int, 3> cmyk_to_rgb255(double c, double m, double y, double k)
  {
    double r = 0.0, g = 0.0, b = 0.0;
    cmyk_to_rgb(c, m, y, k, r, g, b);

    return {static_cast<int>(std::lround(255.0 * r)),
            static_cast<int>(std::lround(255.0 * g)),
            static_cast<int>(std::lround(255.0 * b))};
  }
}
