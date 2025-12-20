
<!--
Copyright (c) 2025, Sascha Willems
SPDX-License-Identifier: MIT
-->

# How to Vulkan in 202X

## About

This repository and the accompanying tutorial demonstrate how to write a "modern" Vulkan application in 202X. The goal is to use as little code as possible for displaying something that's more than just a basic colored triangle. 

Vulkan has been released almost 10 years ago, and a lot has changed. Version 1.0 had to make many concessions to support a broad range of GPUs across desktop and mobile. Some of the initial concepts like render passes turned out to be not so optimal, and have been replaced by alternatives. Not only did the API mature, but so did the ecosystem giving us e.g. new options for writing shaders in languages different than GLSL.

And so for this tutorial we will be using [Vulkan 1.3](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_3.html) as a baseline. This gives us access to (almost all) features that make Vulkan easier to use while still supporting a wide range of GPUs.

tl;dr: Doing Vulkan in 202X can be very different from doing Vulkan in 2016. That's what I hope to show with this.

## Target audience

The tutorial is focused on writing actual Vulkan code and getting things up and running as fast as possible (possibly in a single afternoon). It won't explain programming, software architecture, graphics concepts or how Vulkan works (in detail). Instead it'll often contain links to relevant information like the [Vulkan specification](https://docs.vulkan.org/). You should bring at least basic knowledge of C/C++ and realtime graphics concepts.

## Goal

At the end of this tutorial we'll see multiple textured objects on screen that can be rotated using the mouse. Source comes in a single file with a few hundred lines of code, no abstractions, hard to read modern C++ language constructs or object-oriented shenanigans. I believe that being able to follow source code from top-to-bottom without having to go through multiple layers of abstractions makes it much easier to follow.

## Libraries

Vulkan is a deliberately explicit API, writing code for it can be very verbose. To concentrate on the interesting parts we'll be using the following libraries:

* [SFML](https://www.sfml-dev.org/) - Windowing and input (among other things not used in this tutorial). Without a library like this we would have to write a lot of platform specific code. Alternatives are [glfw](https://www.glfw.org/) and [SDL](https://www.libsdl.org/).
* [Volk](https://github.com/zeux/volk) - Meta-loader that simplifies loading of Vulkan functions.
* [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Simplifies dealing with memory allocations. Removes some of the verbosity around memory management.
* [glm](https://github.com/g-truc/glm) - A mathematics library with support for things like matrices and vectors.
* [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - Single file loader for the obj 3D format.
* [dds-ktx](https://github.com/septag/dds-ktx) - Portable single header library for loading images from KTX files. This will be used for loading textures. The official alternative would be [KTX-Software](https://github.com/KhronosGroup/KTX-Software), but it's a large dependency.

> **Note:** None of these are required to work with Vulkan. They making working with Vulkan easier though and some like VMA and Volk are widely used.

## Programming language

We'll use C++ 20, mostly it's designated initializers. They help with Vulkan's verbosity and improve code readability. Aside from that we won't be using any modern language features and also work with the C Vulkan headers instead of the [C++](https://github.com/KhronosGroup/Vulkan-Hpp) ones. Aside from personal preferences this is done to make this tutorial as approachable as possible, even for people that don't work with C++.

## Shading language

Vulkan does consume shaders in an intermediate format called [SPIR-V](https://www.khronos.org/spirv/). This decouples the API from the actual shading language. Initially only GLSL was supported, but in 2025 there are more and better options. One of those is [Slang](https://github.com/shader-slang) and that's what we'll be using for this tutorial. The language itself is more modern than GLSL and offers some convenient features.

## Build system

Our build system will be [CMake](https://cmake.org/). Similar to my approach to writing code, things will be kept as simple as possible with the added benefit of being able to follow this tutorial with a wide variety of C++ compilers and IDEs.

To create build files for your IDE, run CMake in the root folder of the project like this:

```bash
cmake -B build -G "Visual Studio 17 2022"
```

This will write a Visual Studio 2022 solution file to the `build` folder. The generator (-G) depends on your IDE, you can find a list of those [here](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html). As an alternative you can use [cmake-gui](https://cmake.org/cmake/help/latest/manual/cmake-gui.1.html).

## Validation layers

Vulkan was designed to minimize driver overhead. While that *can* result in better performance, it also removes many of the safeguards that APIs like OpenGL had and puts that responsibility into your hands. If you misuse Vulkan the driver is free to crash. So even if your app works on one GPU, it doesn't guarantee that it works on others. On the other hand, the Vulkan specification defines valid usages for all functionality. And with the [validation layers](https://github.com/KhronosGroup/Vulkan-ValidationLayers), an easy-to-use tool to check for that exists. 

Validation layers can be enabled in code, but the easier option is to download the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) and enable the layers via the [Vulkan Configurator GUI](https://vulkan.lunarg.com/doc/view/latest/windows/vkconfig.html). Once they're enabled, any improper use of the API will be logged to the command line window of our application.

> **Note:** You should always have the validation layers enabled when developing with Vulkan. This makes sure you write spec-compliant code that properly works on other systems.

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

Most important is `apiVersion`, which tells Vulkan that we want to use Vulkan 1.3. Using a higher api version gives us more features out-of-the box that otherwise would have to be used via extensions. [Vulkan 1.3](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_3.html) is widely supported and adds a lot of features to the Vulkan core that make it easier to use. `pApplicationName` can be used to identify your application.

> **Note:** A structure member you'll see a lot is `sType`. The driver needs to know what structure type it has to deal with, and with Vulkan being a C-API there is no other way than specifying it via structure member.

The instance also needs to know about the extensions you want to use. As the name implies, these are used to extend the API. As instance creation (and some other things) are platform specific, the instance needs to know what platform specific extensions you want to use. For Windows e.g. you'd use [VK_KHR_win32_surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_win32_surface.html) and [VK_KHR_android_surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_android_surface.html) for Android and so on for other platforms.

> **Note:** There are two extension types in Vulkan. Instance and device extensions. The former are mostly global, often platform specific extensions independent of your GPU, the latter are based on your GPU's capabilities.

That would mean we'd have to write platform specific code. **But** with a library like SFML we don't have to do that, instead we ask SFML for the platform specific instance extensions:

```cpp
const std::vector<const char*> instanceExtensions{ sf::Vulkan::getGraphicsRequiredInstanceExtensions() };
```

So no more need to worry about platform specific things. With the application info and the required extensions set up, we can create our instance:

```cpp
VkInstanceCreateInfo instanceCI{
	.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	.pApplicationInfo = &appInfo,
	.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
	.ppEnabledExtensionNames = instanceExtensions.data(),
};
chk(vkCreateInstance(&instanceCI, nullptr, &instance));
```

This is very simple. We pass our application info and both the names and number of instance extensions that SFML gave us (for the platform we're compiling for). Calling [`vkCreateInstance`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateInstance.html) creates our instance.

> **Note:** Most Vulkan functions can fail and as such have a return code of type [`VkResult`](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html). We use a small inline function called `chk` to check that return code and in case of an error we exit the application. In a real-world application you should do more sophisticated error handling.

## Queues

In Vulkan, work is not directly submitted to a device but rather to a queue. A queue abstracts access to a piece of hardware (graphics, compute, transfer, video, etc.). They are organized in queue families, with each family describing a set of queues with common functionality. Available queue types differ between GPUs. As we'll only do graphics operations, we need to just find one queue family with graphics support. This is done by checking for the [`VK_QUEUE_GRAPHICS_BIT`](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFlagBits.html) flag:

```cpp
uint32_t queueFamilyCount{ 0 };
vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, nullptr);
std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies.data());
uint32_t queueFamily{ 0 };
for (size_t i = 0; i < queueFamilies.size(); i++) {
	if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
		queueFamily = i;
		break;
	}
}
```

> **Note:** In the real world you'll rarely find GPUs that don't have a queue family that supports graphics. Also most of the time the first queue family supports both graphics and compute (as well as presentation, more on that later).

For our next step we need to reference that queue family using a [`VkDeviceQueueCreateInfo`](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceQueueCreateInfo.html). While we don't do that, it's possible to request multiple queues from the same family. That's why we need to specify priorities in `pQueuePriorities` (in our case just one). With multiple queues from the same family, a driver might use that information to prioritize work:

```cpp
const float qfpriorities{ 1.0f };
VkDeviceQueueCreateInfo queueCI{
	.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	.queueFamilyIndex = queueFamily,
	.queueCount = 1,
	.pQueuePriorities = &qfpriorities
};
```

## Device setup

Now that we have a connection to the Vulkan library and know what queue family we want to use, we need to get a handle to the GPU. This is called a **device** in Vulkan. Vulkan distinguishes between physical and logical devices. The former presents the actual device (usually the GPU), the latter presents a handle to that device's Vulkan implementation which the application will interact with.

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

One thing that's also part of device creation is requesting features and extensions we want to use. But our instance was created with Vulkan 1.3 as a baseline, which gives us almost all the features we want to use. So we only have to request the [`VK_KHR_swapchain`](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_swapchain.html) extension in order to be able to present something to the screen.

Similar to instance creation, using Vulkan 1.3 as a baseline already gives us most of the required functionality. But we'll use dynamic rendering, so we need to set that via the [`dynamicRendering`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html#_members) member of the Vulkan 1.3 feature information. We also want to use anisotropic filtering textures images, so we set [`samplerAnisotropy`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html#_members) in the Vulkan 10 feature information:

```cpp
const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
const VkPhysicalDeviceVulkan13Features enabledVk13Features{
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
	.dynamicRendering = true
};
const VkPhysicalDeviceFeatures enabledVk10Features{
	.samplerAnisotropy = VK_TRUE
};
```

> **Note:** The Vulkan headers have defines for all extensions (like `VK_KHR_SWAPCHAIN_EXTENSION_NAME`) that you can use instead of writing their name as string. This helps to avoid typos in extension names.

With everything in place, we can create a logical device passing all required data: features (for the different core versions), extensions and the queue families we want to use:

```cpp
VkDeviceCreateInfo deviceCI{
	.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	.pNext = &enabledVk13Features,
	.queueCreateInfoCount = 1,
	.pQueueCreateInfos = &queueCI,
	.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
	.ppEnabledExtensionNames = deviceExtensions.data(),
	.pEnabledFeatures = &enabledVk10Features
};
chk(vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device));
```

> **Note:** Another Vulkan struct member you're going to see often is `pNext`. This can be used to create a linked list of structures that are passed into a function call. It's often used to pass extension structures. The driver then uses the `sType` member of each structure in that list to identify said structure's type.

We also need a queue to submit our graphics commands to, which we can now request from the device we just created:

```cpp
vkGetDeviceQueue(device, queueFamily, 0, &queue);
```

## Setting up VMA

Vulkan is an explicit API, which also applies to memory management. As noted in the list of libraries we will be using the [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) to simplify this as far as possible.

VMA provides an allocator used to allocate memory from. This needs to be set up for your project once. For that we pass in pointers to some common Vulkan functions, our Vulkan instance and device:

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

> **Note:** VMA also uses [`VkResult`](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html) return codes, we can use the same `chk` function to check VMA's function results.

## Window and surface

To "draw" something in Vulkan (the correct term would be "present an image", more on that later) we need a surface. Most of the time, a surface is taken from a window. Creating both is platform specific, as mentioned in the [instance chapter](#instance-setup). So in theory, this *would* require us to write different code paths for all platform we want to support (Windows, Linux, Android, etc.).

But that's where libraries like SFML come into play. They take care of all the platform specifics for us, so that part becomes dead simple.

> **Note:** Libraries like SFML, gflw or SDL also take care of other platform specific functionality like input, audio and networking (to a varying degree).

First we create a window:

```cpp
auto window = sf::RenderWindow(sf::VideoMode({ 1280, 720u }), "How to Vulkan");
```

And then request a Vulkan surface for that window:

```cpp
chk(window.createVulkanSurface(instance, surface));
```

For the following chapter(s) we'll need to know the properties surface we just created, so we get them via [`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html) and store it for future reference:

```cpp
VkSurfaceCapabilitiesKHR surfaceCaps{};
chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));
```

## Swapchain

To visually present something to a surface (in our case, the window) we need to create a swapchain. It's basically a series of images that you enqueue to the presentation engine of the operating system. The [`VkSwapchainCreateInfoKHR`](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html) is pretty extensive and requires some explanation.

```cpp
const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
VkSwapchainCreateInfoKHR swapchainCI{
	.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	.surface = surface,
	.minImageCount = surfaceCaps.minImageCount,
	.imageFormat = imageFormat,
	.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
	.imageExtent{ .width = surfaceCaps.currentExtent.width, .height = surfaceCaps.currentExtent.height },
	.imageArrayLayers = 1,
	.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	.queueFamilyIndexCount = queueFamily,
	.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	.presentMode = VK_PRESENT_MODE_FIFO_KHR
};
chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
```

We're using the 4 component color format `VK_FORMAT_B8G8R8A8_SRGB` with a non-linear sRGB [color space](https://docs.vulkan.org/refpages/latest/refpages/source/VkColorSpaceKHR.html) `VK_COLORSPACE_SRGB_NONLINEAR_KHR`. This combination is guaranteed to be available everywhere. Different combinations would require checking for support. `minImageCount` will be the minimum no. of images w get from the swapchain. This value varies between GPUs, hence why we use the information we earlier requested from the surface. `presentMode` defines the way in which images are presented to the screen. [`VK_PRESENT_MODE_FIFO_KHR`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html#) is a v-synced mode and the only mode guaranteed to be available everywhere.

> **Note:** The swapchain setup shown here is a bare minimum. In a real-world application this part can be quite complicated, as you might have to adjust this based on user settings. One example would be HDR capable devices, where you'd need to use a different image format and color space.

Something special about the swapchain is that it's images are not owned by the application, but rather by the swapchain. So instead of explicitly creating these on our own, we request them from the swapchain. This will give as at least as many images are set by `minImageCount`:

```cpp
uint32_t imageCount{ 0 };
vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
swapchainImages.resize(imageCount);
vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
swapchainImageViews.resize(imageCount);
```

## Depth attachment

The swapchain images give us a way of storing color values. But we'll render three-dimensional objects and want make sure they're properly displayed, no matter from what perspective you look at them, or in which order their triangles are rasterized. That's done via [depth testing](https://docs.vulkan.org/spec/latest/chapters/fragops.html#fragops-depth) and to use that we need to have a depth attachment.

The properties of the depth image are defined in a [`VkImageCreateInfo`](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html) structure. Some of these are similar to those found at swapchain creation:

```cpp
const VkFormat depthFormat{ VK_FORMAT_D24_UNORM_S8_UINT };
VkImageCreateInfo depthImageCI{
	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	.imageType = VK_IMAGE_TYPE_2D,
	.format = depthFormat,
	.extent{.width = window.getSize().x, .height = window.getSize().y, .depth = 1 },
	.mipLevels = 1,
	.arrayLayers = 1,
	.samples = VK_SAMPLE_COUNT_1_BIT,
	.tiling = VK_IMAGE_TILING_OPTIMAL,
	.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
};
```	
> **Note:** We use a fixed depth format (`VK_FORMAT_D24_UNORM_S8_UINT`). This is a [mandatory format](https://docs.vulkan.org/spec/latest/chapters/formats.html#features-required-format-support) meaning it's supported in every Vulkan implementation.

The image is 2D and uses a format with support for depth. We don't need multiple mip levels or Layers. Using optimal tiling with [`VK_IMAGE_TILING_OPTIMAL`](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageTiling.html) makes sure the image is stored in a format best suited for the GPU. We also need to state our desired usage cases for the image, which is [`VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageUsageFlagBits.html) as we'll use it as the depth attachment for our render output (more on that later). The initial layout defines the image's content, which we don't have to care about, so we set that to[`VK_IMAGE_LAYOUT_UNDEFINED`](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageLayout.html).

This is also the first time we'll use VMA to allocate something in Vulkan. Memory allocation for buffers and images in Vulkan is verbose yet often very similar. With VMA we can do away with a lot of that. VMA also takes care of selecting the correct memory types and usage flags, something that would otherwise require a lot of code to get proper.

```cpp
VmaAllocationCreateInfo allocCI{
	.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	.usage = VMA_MEMORY_USAGE_AUTO
};
vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage, &depthImageAllocation, nullptr);
```	

One thing that makes VMA so convenient is [`VMA_MEMORY_USAGE_AUTO`](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/choosing_memory_type.html). This usage flag will have VMA select the required usage flags automatically based on the other values you pass in for the allocation and/or buffer create info. There are some cases where you might be better off explicitly stating usage flags, but in most cases, the auto flag ist he perfect fit. The `VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT` flag tells VMA to create a separate memory allocation for this resource, which is recommended for e.g. large image attachments.

> **Note:** We only need a single image, even if we do double buffering in other places. That's because the image is only every accessed by the GPU and the GPU can only ever write to a single depth image at a time. This differs from resources shared by the CPU and the GPU, but more on that later.

Images in Vulkan are not accessed directly, but rather through [views](https://docs.vulkan.org/spec/latest/chapters/resources.html#VkImageView), a common concept in programming. This adds flexibility and allows different access patterns for the same image.

```cpp
VkImageViewCreateInfo depthViewCI{ 
	.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	.image = depthImage,
	.viewType = VK_IMAGE_VIEW_TYPE_2D,
	.format = depthFormat,
	.subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
};
chk(vkCreateImageView(device, &depthViewCI, nullptr, &depthImageView));
```

We need a view to the image we just created and we want to access it as a 2D view. The `subresourceRange` specifies the part of the image we want to access via this view. For images with multiple layers or (mip) levels you could do separate image views to any of these and access an image in different ways. The `aspectMask` refers to the aspect of the image that we want to access via the view. This can be color, stencil, or (in our case) the depth part of the image.

With both the image and the image view created, our depth attachment is now ready to be used later on for rendering.

## Mesh data

From Vulkan's perspective there is no technical difference between drawing a single triangle or a complex mesh with thousands of triangles. Both result in some sort of buffer that the GPU will read data from. The GPU does not care where that data comes from. But from a learning experience it's much better to load an actual 3D object instead of displaying a triangle from hardcoded vertex data. That's our next step.

There are plenty of formats around for storing 3D models. [glTF](https://www.khronos.org/Gltf) for example offers a lot of features and is extensible in a way similar to Vulkan. But we want to keep things simple, so we'll be using the [Wavefront .obj format](https://en.wikipedia.org/wiki/Wavefront_.obj_file) instead. As far as 3D formats go, it won't get more plain than this. And it's supported by many tools like [Blender](https://www.blender.org/).

First we declare a struct for the vertex data. Aside from vertex positions, we also need texture coordinates. These are colloquially abbreviated as [uv](https://en.wikipedia.org/wiki/UV_mapping):

```cpp
struct Vertex {
	glm::vec3 pos;
	glm::vec2 uv;
};
```

We're using the [tinyobjloader library](https://github.com/tinyobjloader/tinyobjloader) to the load .obj files. It does all the parsing and gives us structured access to the data contained in that file:

```cpp
// Mesh data
tinyobj::attrib_t attrib;
std::vector<tinyobj::shape_t> shapes;
std::vector<tinyobj::material_t> materials;
chk(tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/monkey.obj"));
```

After a successful call to `LoadObj`, we can access the vertex data stored in the selected .obj file. `attrib` contains a linear array of the vertex data, `shapes` contains indices into that data. `materials` won't be used, we'll do our own shading. 

> **Note:** The .obj format is a bit dated nd doesn't match modern 3D pipelines in all aspects. One such aspect is indexing of the vertex data. Due to how .obj files are structured we end up with one index per vertex, which limits the effectiveness of indexed rendering. In a real-world application you'd use better formats that work well with indexed rendering.

We'll be using interleaved vertex attributes meaning that (in memory) for every vertex three floats for position data are followed by two floats for texture coordinates. For that to work we need to combine the position and texture coordinate data that tinyobj provides us with:

```cpp
const VkDeviceSize indexCount{shapes[0].mesh.indices.size()};	
std::vector<Vertex> vertices{};
std::vector<uint16_t> indices{};
// Load vertex and index data
for (auto& index : shapes[0].mesh.indices) {
	Vertex v{
		.pos = { attrib.vertices[index.vertex_index * 3], -attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2] },
		.uv = { attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2+ 1] }
	};
	vertices.push_back(v);
	indices.push_back(indices.size());
}
```

> **Note:** The value of the position's y-axis and the texture coordinate's v-axis are flipped to accommodate for Vulkan's coordinate system. Otherwise the model and the texture image would appear upside down.

With the data stored in an interleaved way we can now upload the data to GPU. In theory we could just keep this in a buffer in CPU's RAM, but that would be a lot slower to access by the GPU than storing it in the GPU's VRAM For that we need to create a buffer that's going to hold the vertex data to be accessed by the GPU:

```cpp
VkDeviceSize vBufSize{ sizeof(Vertex) * vertices.size() };
VkDeviceSize iBufSize{ sizeof(uint16_t) * indices.size() };
VkBufferCreateInfo bufferCI{
	.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	.size = vBufSize + iBufSize,
	.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
};
```

Instead of having separate buffers for vertices and indices, we'll put both into the same buffer. That explains why the `size` of the buffer is calculated from the size of both vertices and index vectors. The buffer [`usage`](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferUsageFlagBits.html) bit mask combination of `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` and `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` signals that intended use case to the driver.

<!--
This is also the first time we'll use VMA to allocate something in Vulkan. Memory allocation for buffers and images in Vulkan is verbose yet often very similar. With VMA we can do away with a lot of that. VMA also takes care of selecting the correct memory types and usage flags, something that would otherwise require a lot of code to get proper.
-->

Similar to creating images earlier on we use VMA to allocate the buffer for storing vertex and index data.

```cpp
VmaAllocationCreateInfo bufferAllocCI{
	.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
	.usage = VMA_MEMORY_USAGE_AUTO
};
chk(vmaCreateBuffer(allocator, &bufferCI, &bufferAllocCI, &vBuffer, &vBufferAllocation, nullptr));
```

We again use `VMA_MEMORY_USAGE_AUTO` to have VMA select the correct usage flags for the buffer. The specific `flags` combination of `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` and `VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT` used here make sure we get a memory type that's located on the device (in VRAM) and accessible by the host. Such memory types initially were only available on systems with a unified memory architecture like mobiles or computers with an integrated GPU. But thanks to [(Re)BAR/SAM](https://en.wikipedia.org/wiki/PCI_configuration_space#Resizable_BAR) even dedicated GPUs can now map at least some of their VRAM into host space and make it accessible via the CPU.

> **Note:** Without this we'd have to create a so-called "staging" buffer on the host, copy data to that buffer and then submit a buffer copy from staging to the GPU side buffer using a command buffer. That would require a lot more code.

The `VMA_ALLOCATION_CREATE_MAPPED_BIT` flag lets us map the buffer, which in turn lets us directly copy data into VRAM:

```cpp
void* bufferPtr{ nullptr };
vmaMapMemory(allocator, vBufferAllocation, &bufferPtr);
memcpy(bufferPtr, vertices.data(), vBufSize);
memcpy(((char*)bufferPtr) + vBufSize, indices.data(), iBufSize);
vmaUnmapMemory(allocator, vBufferAllocation);
```

## Frames-in-flight

Short intro on the whys

```cpp
constexpr uint32_t maxFramesInFlight{ 2 };
std::array<UniformBuffers, maxFramesInFlight> uniformBuffers;
```

## Descriptors

Short explanation of what they are, why the are needed and that for more complex setups descriptor indexing makes life easier.

```cpp
VkDescriptorPoolSize poolSizes[2]{
	{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = maxFramesInFlight },
	{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
};
VkDescriptorPoolCreateInfo descPoolCI{
	.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	.maxSets = maxFramesInFlight + 1,
	.poolSizeCount = 2,
	.pPoolSizes = poolSizes
};
chk(vkCreateDescriptorPool(device, &descPoolCI, nullptr, &descriptorPool));
```

desc set layout = defines interface between application and shader

## Uniform buffers

Used to pass data to the shaders.

More flexible than passing fixed vertex data, but also more involved.

Explain why they're called like that.

"that is uniform (constant) for all vertices/fragments within a single draw call"

" a variable is called "uniform" because its value remains constant (uniform) across all shader invocations for a single draw call. "

@todo: maybe move to top

```cpp
VkDescriptorSetLayoutBinding descLayoutBinding{
	.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	.descriptorCount = 1,
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
};
VkDescriptorSetLayoutCreateInfo descLayoutCI{
	.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	.bindingCount = 1,
	.pBindings = &descLayoutBinding
};
chk(vkCreateDescriptorSetLayout(device, &descLayoutCI, nullptr, &descriptorSetLayout));
`` 

```cpp
for (auto i = 0; i < maxFramesInFlight; i++) {
	VkBufferCreateInfo uBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(glm::mat4), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT };
	VmaAllocationCreateInfo uBufferAllocCI{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	chk(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI, &uniformBuffers[i].buffer, &uniformBuffers[i].allocation, nullptr));
	vmaMapMemory(allocator, uniformBuffers[i].allocation, &uniformBuffers[i].mapped);
	VkDescriptorSetAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &descriptorSetLayout };
	chk(vkAllocateDescriptorSets(device, &allocInfo, &uniformBuffers[i].descriptorSet));
	VkDescriptorBufferInfo descBuffInfo{ .buffer = uniformBuffers[i].buffer, .range = VK_WHOLE_SIZE };
	VkWriteDescriptorSet writeDescSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = uniformBuffers[i].descriptorSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &descBuffInfo, };
	vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);
}
```

## Sync objects

No need to deal with in GL (driver cared for you), but Vulkan requires this. One of the hardest part to get right. Doing stuff wrong might work on one GPU but fail on the other. Note on using syncval in Vkconfig. One thing old apis did completely implicitly was GPU/CPU sync. This is explicit in Vulkan. We need to make sure the CPU doesn't start writing to resources still in use by the GPU (read-after-write hazard). As for semaphores, link to guide chapter. Explain two sync objects. Fences for CPU/GPU sync, Semaphores for GPU-only sync. Mention timeline sempahores, not used here due to WSI and sync being pretty eas anyway.

```cpp
VkSemaphoreCreateInfo semaphoreCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
VkFenceCreateInfo fenceCI{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
for (auto i = 0; i < maxFramesInFlight; i++) {
	chk(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
	chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
}
renderSemaphores.resize(swapchainImages.size());
for (auto& semaphore : renderSemaphores) {
	chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
}
```

## Command buffers

First used to submit copy for image, so need to explained before that. Work in VK isn't directly issued but rather compiled into command buffers. CBs are then submitted to a queue. That way you can create CBs in multiple threads or submit to different queues.

```cpp
VkCommandPoolCreateInfo commandPoolCI{
	.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	.queueFamilyIndex = queueFamily
};
chk(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
VkCommandBufferAllocateInfo cbAllocCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .commandBufferCount = maxFramesInFlight };
chk(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers.data()));
```

## Loading a texture

Pretty verbose in Vulkan. VK_EXT_host_image_copy simplifies this, but not widely available.

## Shaders

As mentioned earlier we'll be using the Slang shading language. Vulkan can't directly load shaders written in such a language though (or GLSL or HLSL). It expects them in the SPIR-V intermediate format. For that we need to compile from Slang to SPIR-V first. There are two approaches to do that: Compile offline using Slang's command line compiler or compile at runtime using Slang's library.

We'll go for the latter as that makes updating shaders a bit easier. With offline compilation you'd have to recompile the shaders every time you change them or find a way to have the build system do that for you. With runtime compilation we'll always use the latest shader version when running our code.

To compile Slang shaders we first create a global Slang session, which is the connection between our application and the Slang library:

```cpp
slang::createGlobalSession(slangGlobalSession.writeRef());
```

Next we create a session to define our compilations scope. We want to compile to SPIR-V, so we need to set the target `format` to `SLANG_SPIRV`. Similar to using a fixed Vulkan version as a baseline we want [SPIR-V 1.4](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_spirv_1_4.html) as our baseline for shaders. This has been added to the core in Vulkan 1.2, so it's guaranteed to be support in our case. We also change the `defaultMatrixLayoutMode` to a column major layout to match the matrix layout to what Vulkan uses:

```cpp
auto slangTargets{ std::to_array<slang::TargetDesc>({ {
	.format{SLANG_SPIRV},
	.profile{slangGlobalSession->findProfile("spirv_1_4")}
} }) };
auto slangOptions{ std::to_array<slang::CompilerOptionEntry>({ {
	slang::CompilerOptionName::EmitSpirvDirectly,
	{slang::CompilerOptionValueKind::Int, 1}
} }) };
slang::SessionDesc slangSessionDesc{
	.targets{targets.data()},
	.targetCount{SlangInt(targets.size())},
	.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
	.compilerOptionEntries{slangTargets.data()},
	.compilerOptionEntryCount{uint32_t(slangTargets.size())}
};
Slang::ComPtr<slang::ISession> slangSession;
slangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());
```

After a call to `createSession` we can use that session to get the SPIR-V. We first load the shader from a file using `loadModuleFromSource` and then use `getTargetCode` to compile all entry points in our shader to SPIR-V:

```cpp
Slang::ComPtr<slang::IModule> slangModule{ slangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr) };
Slang::ComPtr<ISlangBlob> spirv;
slangModule->getTargetCode(0, spirv.writeRef());
```

To then use our shader in our graphics pipeline (see below) we need to create a shader module. These are containers for compiled SPIR-V shaders. To create such a module, we pass the SPIR-V compiled by Slang to [`vkCreateShaderModule`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateShaderModule.html):

```cpp
VkShaderModuleCreateInfo shaderModuleCI{
	.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	.codeSize = spirv->getBufferSize(),
	.pCode = (uint32_t*)spirv->getBufferPointer()
};
VkShaderModule shaderModule{};
chk(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));
```

## The shader

The shader itself is pretty simple. We have entry points for a vertex (`[shader("vertex")]`) and a fragment shader (`[shader("fragment")]`). The `VSInput` structure that is passed to the main function of the vertex shader passes the vertex attributes from the application into said shader.`ConstantBuffer<UBO>` maps the uniform data containing our model-view-projection matrix. The vertex shader transforms the vertex data with that and uses `VSOutput` to pass that to the fragment shader. That then uses `samplerTexture` to sample from the texture and writes to the color attachment.

> **Note:** Slang lets us put all shader stages into a single file. That removes the need to duplicate the shader interface or having to put that into shared includes. It also makes it easier to read (and edit) the shader.

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

## Graphics pipeline

Only explain relevant parts.

## Render loop

### Sync

Make sure we don't record a certain command buffer on the CPU until execution of it has finished on the GPU. That's what fences can be used for.

### Update shader data

### Draw

Tell why we recreate CBs (it's cheap and simplifies things)

### Event polling

SFML specific, maybe separate chapter for the resize stuff

## Cleaning up

Destroying Vulkan resources is just as explicit as creating them. In theory you could exit the application without doing that and have the operating system clean up for you instead. But properly cleaning up after you is common sense and so we do that. First we want to make sure that no resources are still in use by the GPU, so we call [vkDeviceWaitIdle](https://docs.vulkan.org/refpages/latest/refpages/source/vkDeviceWaitIdle.html). This waits until the GPU has completed all outstanding operations.

```cpp
chk(vkDeviceWaitIdle(device));
```

Once that call has successfully finished, we can start cleaning up all the Vulkan GPU objects we created for this tutorial:

```cpp
for (auto i = 0; i < maxFramesInFlight; i++) {
	vkDestroyFence(device, fences[i], nullptr);
	vkDestroySemaphore(device, presentSemaphores[i], nullptr);
	...
}
vmaDestroyImage(allocator, depthImage, depthImageAllocation);
...
vkDestroyCommandPool(device, commandPool, nullptr);
vmaDestroyAllocator(allocator);
vkDestroyDevice(device, nullptr);
vkDestroyInstance(instance, nullptr);
```

Ordering of commands only matters for the VMA allocator, device and instance. These should only be destroyed after all objects created from them. The instance should be deleted last, that way we'll be notified by the validation layers (when enabled) of every object we forgot to properly delete. One resource you don't have to explicitly destroy are the command buffers. Calling [vkDestroyCommandPool](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyCommandPool.html) will implicitly free all command buffers allocated from that pool.

## Closing words