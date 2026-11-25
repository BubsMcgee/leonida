#pragma once

#include <filesystem>
#include <d3d12.h>

namespace rt::package_splash {

// Called before the title starts. An absent or disabled shell visual leaves presentation to the title.
void configure(const std::filesystem::path& package);
bool configured();
bool paint_window(HWND window);
ID3D12Resource* create_texture(ID3D12Device* device, UINT width, UINT height);

// Drops the decoded image now (the window paint and the splash texture both have their copy). The next paint or
// texture decode loads it again; call this to give the memory back as soon as the splash is no longer needed.
void release_pixels();

}  // namespace rt::package_splash
