// Native reproducers for Blend2D upstream #267/#268, #269/#270, #271, #272/#273.
// The stroke cases derive from Jeff Witt's upstream reports and tests.
// Keep these direct calls: docling-parse #407 flattens stroked PDF cubics and
// prevents the Python PDF fixtures from reaching the affected native routines.

#include <blend2d/blend2d.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

static bool affine()
{
    BLImage source(16, 16, BL_FORMAT_PRGB32);
    BLContext source_context(source);
    if (source_context.fill_all(BLRgba32(0xFF808080u)) != BL_SUCCESS ||
        source_context.end() != BL_SUCCESS) { return false; }

    BLImage images[2];
    for (int reference = 0; reference < 2; ++reference)
    {
        if (images[reference].create(511, 10, BL_FORMAT_PRGB32) != BL_SUCCESS)
        { return false; }
        BLContextCreateInfo options{};
        options.flags = reference ? BL_CONTEXT_CREATE_FLAG_DISABLE_JIT : 0;
        BLContext context(images[reference], options);
        if (context.fill_all(BLRgba32(0xFFFFFFFFu)) != BL_SUCCESS ||
            context.apply_transform(BLMatrix2D(1.1372183005498808, 0.0012802436069027732,
                                               0.0044605010442717481, 1.1372183005498808,
                                               -288.41887602788347, 2.3954065681413779)) != BL_SUCCESS ||
            context.blit_image(BLRect(0, 0, 2592, 3312), source, BLRectI(0, 0, 16, 16)) != BL_SUCCESS ||
            context.end() != BL_SUCCESS) { return false; }
    }
    BLImageData actual, expected;
    if (images[0].get_data(&actual) != BL_SUCCESS ||
        images[1].get_data(&expected) != BL_SUCCESS) { return false; }
    for (int y = 0; y < 10; ++y)
    {
        const auto* a = static_cast<const unsigned char*>(actual.pixel_data) + y * actual.stride;
        const auto* b = static_cast<const unsigned char*>(expected.pixel_data) + y * expected.stride;
        if (std::memcmp(a, b, 511 * 4) != 0) { return false; }
    }
    return true;
}

static bool stroke(std::string_view name)
{
    BLPath path;
    BLStrokeOptions options;
    options.width = 1.0;
    options.start_cap = BL_STROKE_CAP_ROUND;
    options.end_cap = BL_STROKE_CAP_ROUND;
    options.join = BL_STROKE_JOIN_ROUND;
    if (name == "stall")
    {
        path.move_to(623.61953000604717, 711.94969687666287);
        path.line_to(624.3877062003628, 712.71734821265682);
        path.cubic_to(624.96383834609946, 712.52543537865824,
                      624.57975024894165, 712.71734821265682,
                      624.96383834609946, 712.33352254465979);
    }
    else if (name == "spike")
    {
        path.move_to(1989.6699680076856, 1068.4518265722766);
        path.line_to(1990.2205198971101, 1068.4518265722766);
        path.cubic_to(1989.9452439523977, 1069.2776544064131,
                      1990.2205198971101, 1068.8647404893447,
                      1989.8076059800417, 1069.2776544064131);
    }
    else if (name == "flat-return" || name == "flat-overshoot")
    {
        options.width = 2.0;
        path.move_to(20, 50);
        if (name == "flat-return") { path.cubic_to(60, 50, 60, 50, 20, 50); }
        else { path.cubic_to(50, 50, 60, 50, 50, 50); }
    }
    else { return false; }

    BLPath outline;
    if (outline.add_stroked_path(path, options, bl_default_approximation_options) != BL_SUCCESS ||
        outline.size() == 0) { return false; }
    BLBox box;
    if (outline.get_bounding_box(&box) != BL_SUCCESS ||
        !std::isfinite(box.x0) || !std::isfinite(box.x1) ||
        !std::isfinite(box.y0) || !std::isfinite(box.y1) ||
        box.x1 <= box.x0 || box.y1 <= box.y0) { return false; }
    if (name == "flat-return" || name == "flat-overshoot")
    {
        const double right = name == "flat-return" ? 51.0 : 54.75;
        return std::abs(box.x0 - 19.0) < 0.05 && std::abs(box.x1 - right) < 0.05;
    }
    BLBox original;
    if (path.get_bounding_box(&original) != BL_SUCCESS) { return false; }
    return box.x0 >= original.x0 - 0.6 && box.y0 >= original.y0 - 0.6 &&
           box.x1 <= original.x1 + 0.6 && box.y1 <= original.y1 + 0.6;
}

int main(int argc, char** argv)
{
    if (argc != 2) { return 2; }
    const std::string_view name(argv[1]);
    const bool passed = name == "affine" ? affine() : stroke(name);
    std::fprintf(passed ? stdout : stderr, "%s: %s\n", argv[1], passed ? "passed" : "failed");
    return passed ? 0 : 1;
}
