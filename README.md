# How to Vulkan (in 2025)

## About

This repository and the accompanying tutorial demonstrate how to write a "modern" Vulkan application in 2025. The goal is to use as little code as possible for displaying something that's more than just a basic colored triangle. 

Vulkan has been released almost 10 years ago, and a lot has changed. Version 1.0 had to make many concessions to support a broad range of GPUs across desktop and mobile. Some of the initial concepts turned out to be not so optimal, e.g. render passes, and have been replaced by alternatives. Not only did the api mature, but so did the ecosystem giving us e.g. new options for writing shaders in languages different than GLSL.

And so for this tutorial we will be using Vulkan 1.3 as a baseline. This gives us access to (almost all) features that make Vulkan easier to use while still supporting a wide range of GPUs.

tl;dr: Doing Vulkan in 2025 can be very different from doing Vulkan in 2016. That's what I hope to show with this.

## Target audience

The tutorial is focused on writing actual Vulkan code and getting things up and running as fast as possible (in an afternoon). It won't explain programming, software architecture, graphics concepts or how Vulkan works (in detail). You should bring at least basic knowledge of C/C++ and graphics programming concepts are required.

## Goal

At the end of this tutorial we'll see a textured quad on screen that can be rotated using the mouse. We also use multi-sampling so we can demonstrate how to use an intermediate render image. All of this will be done in a single source file with a few hundred lines of code, no abstraction, hard to read modern C++ language constructs or object-oriented shenanigans. I believe that being able to follow source code from top-to-bottom without having to go through multiple layers of abstractions makes it much easier to follow.

## Libraries

Vulkan is a deliberately explicit api, writing code it can be very verbose. To concentrate on the interesting parts we'll be using several libraries.

* [SFML](https://www.sfml-dev.org/) - Windowing and input (among other things not used in this tutorial). Without a library like this we would have to write platform specific code for these. Alternatives are [glfw](https://www.glfw.org/) and [SDL](https://www.libsdl.org/).
* [Volk](https://github.com/zeux/volk) - Meta-loader for Vulkan that simplifies loading of Vulkan functions.
* [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Simplifies dealing with memory allocations. Removes some of the verbosity around Vulkan's memory management.
* [glm](https://github.com/g-truc/glm) - A mathematics library with support for often-used things like matrices and vectors.
* [dds-ktx](https://github.com/septag/dds-ktx) - Portable single header library for loading images from KTX files. This will be used for loading textures. The official alternative would be [KTX-Software](https://github.com/KhronosGroup/KTX-Software), but it's a large dependency.

> **Note:** None of these are required to work with Vulkan. Some of these are widely used though and make working with Vulkan easier.

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

## Instance setup

The first thing we need is a Vulkan instance. It connects the application to Vulkan and as such is the base for everything that follows.

Setting up the instance consists of passing information about your application:

```cpp
VkApplicationInfo appInfo{
	.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	.pApplicationName = "How to Vulkan",
	.apiVersion = VK_API_VERSION_1_3
};
```

Most import is `apiVersion`, which tells Vulkan that we want to use Vulkan 1.3. Using a higher api version gives us more features out-of-the box that otherwise would have to be used via extensions. [Vulkan 1.3](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_3.html) is widely supported and adds a lot of features to the Vulkan core that make it easier to use. `pApplicationName` can be used to identify your application.

> **Note:** You'll see the `sType` member a lot when writing Vulkan using the C-API. The driver needs to know what structure type it has to deal with, and with Vulkan being a C-API there is no other way than specifying it via structure member.

The instance also needs to know about the extensions you want to use. As the name implies, these are used to extend the api. As instance creation (and some other things) are platform specific, the instance needs to know what platform specific extensions you want to use. For Windows e.g. you'd use [VK_KHR_win32_surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_win32_surface.html) and [VK_KHR_android_surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_android_surface.html) for Android. 

That would mean we'd have to write platform specific code. **But** with a library like SFML we don't have to do that, instead we ask SFML for the platform specific instance extensions:

```cpp
const std::vector<const char*> instanceExtensions{ sf::Vulkan::getGraphicsRequiredInstanceExtensions() };
```

So no more need to worry about platform specific things. With the application info and the required extensions set up, we can create our instance:

> **Note:** There are two extension types in Vulkan. Instance and device extensions. The former are mostly global, often platform specific extensions independent of your GPU, the latter are based on your GPU's capabilities.

```cpp
VkInstanceCreateInfo instanceCI{
	.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	.pApplicationInfo = &appInfo,
	.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
	.ppEnabledExtensionNames = instanceExtensions.data(),
};
chk(vkCreateInstance(&instanceCI, nullptr, &instance));
```

This is very simple. We pass our application info and both the names and number of instance extensions that SFML gave us. Calling [`vkCreateInstance`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateInstance.html) creates our instance.

> **Note:** Most Vulkan functions can fail and as such have a return code of type [`VkResult`](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html). We use a small inline function called `chk` to check that return code and in case of an error we exit the application. In a real-world application you should do more sophisticated error handling.

## Queues

@todo: Should use code to properly select a queue famile

## Device setup

Now that we have a connection to the Vulkan library, we need to get a handle to the GPU. This is called a **device** in Vulkan. Vulkan distinguishes between physical and logical devices. The former presents the actual device (usually the GPU), the latter presents a handle to that device's Vulkan implementation which the application will interact with.

> **Note:** When dealing with Vulkan a commonly used term is implementation. This refers to something that implements the Vulkan API. Usually it's the driver for your GPU, but it also could be a CPU based software implementation. To keep things simple we'll be using the term GPU for the rest of the tutorial.

First we need to get a list physical devices currently available:

```cpp
uint32_t deviceCount{ 0 };
chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
std::vector<VkPhysicalDevice> devices(deviceCount);
chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
```

> **Note:** Having to call functions that return some sort of list twice is common in the Vulkan C-API. The first call will return the number of elements, which is then used to properly size the result list. The second call then fills the actual result list.

After the second call to [`vkEnumeratePhysicalDevices`](https://docs.vulkan.org/refpages/latest/refpages/source/vkEnumeratePhysicalDevices.html) we have a list of Vulkan capable devices in `devices`. On most systems there will only be one device, so for simplicity we use the first physical device. In a real-world application you could let the user select different devices, e.g. via command line arguments.

One thing that's also part of device creation is requesting features and extensions we want to use. But our instance was created with Vulkan 1.3 as a baseline, which gives us almost all the features we want to use. So we only have to request the [`VK_KHR_swapchain`](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_swapchain.html) extension in order to be able to present something to the screen and the [`.samplerAnisotropy`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html#_members) feature so we can use anisotropic filtering for texture images:

```cpp
const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
const VkPhysicalDeviceFeatures enabledFeatures{ .samplerAnisotropy = VK_TRUE };
```

> **Note:** The Vulkan headers have defines for all extensions (like `VK_KHR_SWAPCHAIN_EXTENSION_NAME`) that you can use instead of writing their name as string. This helps to avoid typos in extension names.

With everything in place, we create a logical device. Queues are also part of the logical device, so they'll be requested at device creation:

```cpp
VkDeviceCreateInfo deviceCI{
	.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	.pNext = &features,
	.queueCreateInfoCount = 1,
	.pQueueCreateInfos = &queueCI,
	.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
	.ppEnabledExtensionNames = deviceExtensions.data(),
	.pEnabledFeatures = &enabledFeatures
};
chk(vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device));
```

We also need a queue to submit our commands to, which we can now request from the device we just created:

```cpp
vkGetDeviceQueue(device, qf, 0, &queue);
```

## Setting up VMA

Vulkan is an explicit API, which also applies to memory management. As noted in the list of libraries we will be using the [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) to simplify this as far as possible.

VMA provides an allocator used to allocate memory from. This needs to be set up for your project. For that we pass in pointers to some common Vulkan functions and the Vulkan instance and device we just created:

```cpp
VmaVulkanFunctions vkFunctions{
	.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
	.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
	.vkCreateImage = vkCreateImage
};
VmaAllocatorCreateInfo allocatorCI{
	.physicalDevice = devices[deviceIndex],
	.device = device,
	.pVulkanFunctions = &vkFunctions,
	.instance = instance
};
chk(vmaCreateAllocator(&allocatorCI, &allocator));
```

> **Note:** VMA also uses [`VkResult`](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html) return codes, we can use the same `chK` for it's function calls.

## Preparing to draw

To "draw" something in Vulkan (the correct term would be "present an image") we need a platform specific surface. Thanks to SFML that's dead simple.

We first create a window:

```cpp
auto window = sf::RenderWindow(sf::VideoMode({ 1280, 720u }), "How to Vulkan");
```

And then 