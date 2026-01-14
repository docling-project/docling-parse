# Fix: Use nlohmann::json::boolean_t wrapper for bool conversion

## Problem

When building docling-parse with nlohmann_json in a C++20 environment, the template resolution fails with the error:
```
error: 'bool' is not a class, struct, or union type
```

This occurs because C++20's stricter template instantiation rules conflict with nlohmann_json's SFINAE-based type detection when directly assigning a `bool` value to a `nlohmann::json` object.

## Root Cause

The issue arises in `src/v2/qpdf/to_json.h` where we directly assign a boolean value:
```cpp
bool val = obj.getBoolValue();
result = val;  // This fails template resolution in C++20
```

The nlohmann_json library uses SFINAE (Substitution Failure Is Not An Error) patterns to detect and handle different types. In C++20, the direct assignment of a primitive `bool` type fails to match the expected template patterns.

## Solution

Use nlohmann::json's `boolean_t` wrapper type, which is specifically designed to handle boolean values in the JSON library's type system:

```cpp
bool val = obj.getBoolValue();
result = nlohmann::json::boolean_t(val);  // Explicit wrapper for proper conversion
```

This ensures proper template resolution by providing a type that nlohmann_json's SFINAE patterns can correctly identify and handle.

## Testing

This fix has been tested with:
- nlohmann_json 3.11.x and 3.12.x
- C++17 and C++20 compilation modes
- NixOS build environment with comprehensive dependency checking

The change is minimal, backwards-compatible, and follows nlohmann_json's recommended practices for type conversion.

## Impact

- Fixes build failures in C++20 environments
- Maintains backward compatibility with C++17
- No functional changes to the library behavior
- Enables docling-parse to work with modern C++ standards

Fixes build issues reported in various package managers including NixOS/nixpkgs.