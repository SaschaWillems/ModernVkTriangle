# How to Vulkan (in 2025)

## About

This repository and the accompanying tutorial demonstrate how to write a "modern" Vulkan application in 2025. The goal is to use as little code as possible for displaying something that's more than just a basic colored triangle. 

Vulkan has been released almost 10 years ago, and a lot has changed. Version 1.0 had to make many concessions to support a broad range of GPUs across desktop and mobile. Some of the initial concepts turned out to be not so optimal, e.g. render passes, and have been replaced by alternatives. Not only did the api mature, but so did the ecosystem giving us e.g. new options for writing shaders in languages different than GLSL.

And so for this tutorial we will be using Vulkan 1.3 as a baseline. This gives us access to (almost all) features that make Vulkan easier to use while still supporting a wide range of GPUs.

tl;dr: Doing Vulkan in 2025 can be very different from doing Vulkan in 2016. That's what I hope to show with this.

## Target audience

The tutorial is focused on writing actual Vulkan code and getting things up and running as fast as possible (<1h). It won't explain programming, software architecture, graphics concepts or how Vulkan works (in detail). You should bring at least basic knowledge of C/C++ and graphics programming concepts are required.

## Goal

At the end of this tutorial we'll see a textured quad on screen that can be rotated using the mouse. We also use multi-sampling so we can demonstrate how to use an intermediate render image. All of this will be done in a single source file with a few hundred lines of code, no abstraction, hard to read modern C++ language constructs or object-oriented shenanigans. I believe that being able to follow source code from top-to-bottom without having to go through multiple layers of abstractions makes it much easier to follow.

## Libraries

Begin an explicit api, writing code for Vulkan is very verbose. To concentrate on the interesting parts we'll be using several libraries. Note that one of those are required to work with Vulkan.

* [SFML](https://www.sfml-dev.org/) - Windowing and input (among other things not used in this tutorial). Without a library like this we would have to write platform specific code for these. Alternatives are [glfw](https://www.glfw.org/) and [SDL](https://www.libsdl.org/).
* [Volk](https://github.com/zeux/volk) - Meta-loader for Vulkan that simplifies loading of Vulkan functions.
* [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Simplifies dealing with memory allocations. Removes some of the verbosity around Vulkan's memory management.
* [glm](https://github.com/g-truc/glm) - A mathematics library with support for often-used things like matrices and vectors.
* [dds-ktx](https://github.com/septag/dds-ktx) - Portable single header library for loading images from KTX files. This will be used for loading textures. The official alternative would be [KTX-Software](https://github.com/KhronosGroup/KTX-Software), but it's a large dependency.

## Programming language

We'll use C++ 20 mostly for designated initializers. They help with Vulkan's verbosity and make it easier to read Vulkan code. Aside from that we won't be using any modern language features and also work with the C (not the [C++](https://github.com/KhronosGroup/Vulkan-Hpp)) Vulkan headers. Aside from personal preferences this is to make this as as approachable as possible, even for people that don't work with C++.

## Build system

Our build system will be [CMake](https://cmake.org/). Similar to my approach to writing code, things will be kept as simple as possible with the added benefit of being able to follow this tutorial with a wide variety of C++ compilers and IDEs.

## Shading language

Vulkan does consume shaders in an intermediate format [SPIR-V](https://www.khronos.org/spirv/). This decouples the api from the actual shading language. Initially only GLSL was supported, but in 2025 there are more and better options. One of those is [Slang](https://github.com/shader-slang) and that's what we'll be using for this tutorial. The language itself is more modern than GLSL and offers some convenient features.

## The shader

Slang lets us put all shader stages into a single file. That removes the need to duplicate the shader interface or having to put that into shared includes. It also makes it easy to read (and edit) the shader.

Our shader will be pretty simple. We have a vertex shader (`[shader("vertex")]`) and a fragment shader (`[shader("fragment")]`). The `VSInput` structure that is passed to the main function of the vertex shader passes the vertex attributes from the application into said shader.`ConstantBuffer<UBO>` maps the uniform data containing our model-view-projection matrix. The vertex shader transforms the vertex data with that and uses `VSOutput` to pass that to the fragment shader. That shader then uses `samplerTexture` to sample from the texture and writes to the color attachment.


```slang
struct VSInput {
	float3 Pos;
	float2 UV;
};

struct UBO {
	float4x4 mvp;
};
[[vk::binding(0,0)]] ConstantBuffer<UBO> ubo;

[[vk::binding(0,1)]] Sampler2D samplerTexture;

struct VSOutput {
	float4 Pos : SV_POSITION;
	float2 UV;
};

[shader("vertex")]
VSOutput main(VSInput input) {
	VSOutput output;
	output.UV = input.UV;
	output.Pos = mul(ubo.mvp, float4(input.Pos.xyz, 1.0));
	return output;
}

[shader("fragment")]
float4 main(VSOutput input) {
	return float4(samplerTexture.Sample(input.UV).rgb, 1.0);
}
```