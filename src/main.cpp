/* Copyright (c) 2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <SFML/Graphics.hpp>
#define VOLK_IMPLEMENTATION
#include <volk/volk.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <fstream>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "slang/slang.h"
#include "slang/slang-com-ptr.h"
#define DDSKTX_IMPLEMENT
#include "dds-ktx/dds-ktx.h"

static inline void chk(VkResult result) {
	if (result != VK_SUCCESS) {
		std::cerr << "Vulkan call returned an error\n";
		exit(result);
	}
}
static inline void chk(bool result) {
	if (!result) {
		std::cerr << "Call returned an error\n";
		exit(result);
	}
}

constexpr uint32_t maxFramesInFlight{ 2 };
uint32_t imageIndex{ 0 };
uint32_t frameIndex{ 0 };
VkInstance instance{ VK_NULL_HANDLE };
VkDevice device{ VK_NULL_HANDLE };
VkQueue queue{ VK_NULL_HANDLE };
VkSurfaceKHR surface{ VK_NULL_HANDLE };
VkSwapchainKHR swapchain{ VK_NULL_HANDLE };
VkCommandPool commandPool{ VK_NULL_HANDLE };
VkPipeline pipeline{ VK_NULL_HANDLE };
VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
VkImage renderImage;
VmaAllocation renderImageAllocation;
VkImageView renderImageView;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
std::array<VkFence, maxFramesInFlight> fences;
std::array<VkSemaphore, maxFramesInFlight> presentSemaphores;
std::vector<VkSemaphore> renderSemaphores;
VmaAllocator allocator{ VK_NULL_HANDLE };
VmaAllocation vBufferAllocation{ VK_NULL_HANDLE };
VkBuffer vBuffer{ VK_NULL_HANDLE };
struct UniformBuffers {
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VkBuffer buffer{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	void* mapped{ nullptr };
};
std::array<UniformBuffers, maxFramesInFlight> uniformBuffers;
VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
struct Texture {
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VkImage image{ VK_NULL_HANDLE };	
	VkImageView view{ VK_NULL_HANDLE };
	VkSampler sampler{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
};
Texture texture;
VkDescriptorSetLayout descriptorSetLayoutTex{ VK_NULL_HANDLE };
Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
glm::vec3 camRotation{ 0.0f };
glm::vec3 camPos{ 0.0f, 0.0f, -2.0f };
sf::Vector2i lastMousePos{};

int main()
{
	volkInitialize();
	// Initialize slang compiler
	slang::createGlobalSession(slangGlobalSession.writeRef());
	auto targets{ std::to_array<slang::TargetDesc>({ {.format{SLANG_SPIRV}, .profile{slangGlobalSession->findProfile("spirv_1_6")} } }) };
	auto options{ std::to_array<slang::CompilerOptionEntry>({ { slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1} } }) };
	slang::SessionDesc desc{ .targets{targets.data()}, .targetCount{SlangInt(targets.size())}, .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR, .compilerOptionEntries{options.data()}, .compilerOptionEntryCount{uint32_t(options.size())} };
	Slang::ComPtr<slang::ISession> slangSession;
	slangGlobalSession->createSession(desc, slangSession.writeRef());
	// Instance
	VkApplicationInfo appInfo{ .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "How to Vulkan", .apiVersion = VK_API_VERSION_1_3 };
	const std::vector<const char*> instanceExtensions{ sf::Vulkan::getGraphicsRequiredInstanceExtensions() };
	VkInstanceCreateInfo instanceCI{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
		.ppEnabledExtensionNames = instanceExtensions.data(),
	};
	chk(vkCreateInstance(&instanceCI, nullptr, &instance));
	volkLoadInstance(instance);
	// Device
	const uint32_t deviceIndex{ 0 };
	uint32_t deviceCount{ 0 };
	chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
	std::vector<VkPhysicalDevice> devices(deviceCount);
	chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
	// Find a queue family for graphics
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
	// Logical device
	const float qfpriorities{ 1.0f };
	VkDeviceQueueCreateInfo queueCI{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamily, .queueCount = 1, .pQueuePriorities = &qfpriorities };
	const VkPhysicalDeviceVulkan13Features enabledVk13Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = true };
	const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	const VkPhysicalDeviceFeatures enabledVk10Features{ .samplerAnisotropy = VK_TRUE };
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
	vkGetDeviceQueue(device, queueFamily, 0, &queue);
	// VMA
	VmaVulkanFunctions vkFunctions{ .vkGetInstanceProcAddr = vkGetInstanceProcAddr, .vkGetDeviceProcAddr = vkGetDeviceProcAddr, .vkCreateImage = vkCreateImage };
	VmaAllocatorCreateInfo allocatorCI{ .physicalDevice = devices[deviceIndex], .device = device, .pVulkanFunctions = &vkFunctions, .instance = instance };
	chk(vmaCreateAllocator(&allocatorCI, &allocator));
	// Window and surface
	auto window = sf::RenderWindow(sf::VideoMode({ 1280, 720u }), "How to Vulkan");
	chk(window.createVulkanSurface(instance, surface));
	VkSurfaceCapabilitiesKHR surfaceCaps{};
	chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));
	// Swap chain
	const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	VkSwapchainCreateInfoKHR swapchainCI{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{ .width = surfaceCaps.currentExtent.width, .height = surfaceCaps.currentExtent.height, },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.queueFamilyIndexCount = queueFamily,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
	chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
	uint32_t imageCount{ 0 };
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
	swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
	swapchainImageViews.resize(imageCount);
	// Render target image
	const VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT;
	VkImageCreateInfo renderImageCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imageFormat,
		.extent{.width = window.getSize().x, .height = window.getSize().y, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = sampleCount,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	vmaCreateImage(allocator, &renderImageCI, &allocCI, &renderImage, &renderImageAllocation, nullptr);
	VkImageViewCreateInfo viewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = renderImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
	chk(vkCreateImageView(device, &viewCI, nullptr, &renderImageView));
	for (auto i = 0; i < imageCount; i++) {
		viewCI.image = swapchainImages[i];
		chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
	}
	// Vertex (Pos 3f, UV 2f) and index buffers
	const std::vector<float> vertices{ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, /**/ -1.0f, 1.0f, 0.0f, 0.0f, 1.0f /**/, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f /**/, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f };;
	std::vector<uint16_t> indices = { 0, 1, 2, /**/ 2, 3, 0 };
	VkDeviceSize vBufSize{ sizeof(float) * vertices.size() }; VkDeviceSize iBufSize{ sizeof(uint16_t) * indices.size() };
	VkBufferCreateInfo bufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = vBufSize + iBufSize, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
	VmaAllocationCreateInfo bufferAllocCI{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	chk(vmaCreateBuffer(allocator, &bufferCI, &bufferAllocCI, &vBuffer, &vBufferAllocation, nullptr));
	void* bufferPtr{ nullptr };
	vmaMapMemory(allocator, vBufferAllocation, &bufferPtr);
	memcpy(bufferPtr, vertices.data(), vBufSize);
	memcpy(((char*)bufferPtr) + vBufSize, indices.data(), iBufSize);
	vmaUnmapMemory(allocator, vBufferAllocation);
	VkCommandPoolCreateInfo commandPoolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = queueFamily };
	chk(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
	// Descriptor pool
	VkDescriptorPoolSize poolSizes[2]{ { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = maxFramesInFlight }, {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 } };
	VkDescriptorPoolCreateInfo descPoolCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = maxFramesInFlight + 1, .poolSizeCount = 2, .pPoolSizes = poolSizes  };
	chk(vkCreateDescriptorPool(device, &descPoolCI, nullptr, &descriptorPool));
	// Uniform buffers
	VkDescriptorSetLayoutBinding descLayoutBinding{.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
	VkDescriptorSetLayoutCreateInfo descLayoutCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1,  .pBindings = &descLayoutBinding };
	chk(vkCreateDescriptorSetLayout(device, &descLayoutCI, nullptr, &descriptorSetLayout));
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
	// Sync objects
	VkSemaphoreCreateInfo semaphoreCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for (auto i = 0; i < maxFramesInFlight; i++) {
		VkCommandBufferAllocateInfo cbAllocCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .commandBufferCount = 1};
		chk(vkAllocateCommandBuffers(device, &cbAllocCI, &commandBuffers[i]));
		VkFenceCreateInfo fenceCI{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		vkCreateFence(device, &fenceCI, nullptr, &fences[i]);
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
	}
	renderSemaphores.resize(swapchainImages.size());
	for (auto& semaphore : renderSemaphores) {
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
	}
	// Texture image
	std::ifstream ktxFile("assets/vulkan.ktx", std::ios::binary | std::ios::ate);
	assert(ktxFile.is_open());
	size_t ktxSize = ktxFile.tellg();
	ktxFile.seekg(std::ios::beg);
	char* ktxData = new char[ktxSize];
	ktxFile.read(ktxData, ktxSize);
	ddsktx_texture_info tc = { 0 };
	ddsktx_parse(&tc, ktxData, ktxSize, nullptr);
	VmaAllocationCreateInfo uImageAllocCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	VkImageCreateInfo texImgCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = {.width = (uint32_t)tc.width, .height = (uint32_t)tc.height, .depth = 1 },
		.mipLevels = (uint32_t)tc.num_mips,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	chk(vmaCreateImage(allocator, &texImgCI, &uImageAllocCI, &texture.image, &texture.allocation, nullptr));
	VkImageViewCreateInfo texVewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = texture.image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = texImgCI.format, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
	chk(vkCreateImageView(device, &texVewCI, nullptr, &texture.view));
	VkDescriptorSetLayoutBinding descLayoutBindingTex{ .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
	VkDescriptorSetLayoutCreateInfo descLayoutTexCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1,  .pBindings = &descLayoutBindingTex };
	chk(vkCreateDescriptorSetLayout(device, &descLayoutTexCI, nullptr, &descriptorSetLayoutTex));
	VkDescriptorSetAllocateInfo texDescSetAlloc{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &descriptorSetLayoutTex };
	chk(vkAllocateDescriptorSets(device, &texDescSetAlloc, &texture.descriptorSet));
	// Sampler
	VkSamplerCreateInfo samplerCI{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = 8.0f,
		.maxLod = (float)texImgCI.mipLevels,
	};
	chk(vkCreateSampler(device, &samplerCI, nullptr, &texture.sampler));
	VkDescriptorImageInfo descTexInfo{ .sampler = texture.sampler, .imageView = texture.view, .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR };
	VkWriteDescriptorSet writeDescSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = texture.descriptorSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &descTexInfo };
	vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);
	// Copy (first mip only)
	ddsktx_sub_data subData;
	ddsktx_get_sub(&tc, &subData, ktxData, ktxSize, 0, 0, 0);
	VkBuffer stagingBuffer{};
	VmaAllocation stagingAllocation{};
	VkBufferCreateInfo stgBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = (uint32_t)subData.size_bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
	VmaAllocationCreateInfo stgAllocCI{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	chk(vmaCreateBuffer(allocator, &stgBufferCI, &stgAllocCI, &stagingBuffer, &stagingAllocation, nullptr));
	void* stagingPtr{ nullptr };
	vmaMapMemory(allocator, stagingAllocation, &stagingPtr);
	memcpy(stagingPtr, subData.buff, subData.size_bytes);
	VkFenceCreateInfo fenceOneTimeCI{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fenceOneTime{};
	chk(vkCreateFence(device, &fenceOneTimeCI, nullptr, &fenceOneTime));
	VkCommandBuffer cbOneTime{};
	VkCommandBufferAllocateInfo cbOneTimeAI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .commandBufferCount = 1 };
	chk(vkAllocateCommandBuffers(device, &cbOneTimeAI, &cbOneTime));
	VkCommandBufferBeginInfo cbOneTimeBI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, };
	vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI);
	VkImageMemoryBarrier barrierTex0{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.image = texture.image,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
	};
	vkCmdPipelineBarrier(cbOneTime, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierTex0);
	VkBufferImageCopy copyRegion{
		.imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .layerCount = 1 },
		.imageExtent{.width = (uint32_t)tc.width, .height = (uint32_t)tc.height, .depth = 1 },
	};
	vkCmdCopyBufferToImage(cbOneTime, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
	VkImageMemoryBarrier barrierTex1{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.image = texture.image,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
	};
	vkCmdPipelineBarrier(cbOneTime, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierTex1);
	vkEndCommandBuffer(cbOneTime);
	VkSubmitInfo oneTimeSI{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cbOneTime };
	chk(vkQueueSubmit(queue, 1, &oneTimeSI, fenceOneTime));
	chk(vkWaitForFences(device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));
	vmaUnmapMemory(allocator, stagingAllocation);
	vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
	delete[] ktxData;
	// Shaders
	Slang::ComPtr<slang::IModule> slangModule{ slangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr) };
	Slang::ComPtr<ISlangBlob> spirv;
	slangModule->getTargetCode(0, spirv.writeRef());
	VkShaderModuleCreateInfo shaderModuleCI{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = spirv->getBufferSize(), .pCode = (uint32_t*)spirv->getBufferPointer() };
	VkShaderModule shaderModule{};
	vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule);
	// Pipeline
	VkDescriptorSetLayout pipelineSetLayouts[2]{ descriptorSetLayout, descriptorSetLayoutTex };
	VkPipelineLayoutCreateInfo pipelineLayoutCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 2, .pSetLayouts = pipelineSetLayouts };
	chk(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
	std::vector<VkPipelineShaderStageCreateInfo> stages{
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = shaderModule, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = shaderModule, .pName = "main" }
	};
	VkVertexInputBindingDescription vertexBinding{ .binding = 0, .stride = sizeof(float) * 5, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> vertexAttributes{
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = sizeof(float) * 3},
	};
	VkPipelineVertexInputStateCreateInfo vertexInputState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertexBinding,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = vertexAttributes.data(),
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
	VkPipelineRasterizationStateCreateInfo rasterizationState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = sampleCount };
	VkPipelineDepthStencilStateCreateInfo depthStencilState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState blendAttachment{ .colorWriteMask = 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlendState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment };
	std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates.data() };
	VkPipelineRenderingCreateInfo renderingCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &imageFormat };
	VkGraphicsPipelineCreateInfo pipelineCI{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCI,
		.stageCount = 2,
		.pStages = stages.data(),
		.pVertexInputState = &vertexInputState,
		.pInputAssemblyState = &inputAssemblyState,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizationState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dynamicState,
		.layout = pipelineLayout
	};
	chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));
	vkDestroyShaderModule(device, shaderModule, nullptr);
	// Render loop
	sf::Clock clock;
	while (window.isOpen()) {
		sf::Time elapsed = clock.restart();
		// Sync
		vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX);
		vkResetFences(device, 1, &fences[frameIndex]);
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
		auto cb = commandBuffers[frameIndex];
		// Update UBO
		glm::quat rotQ = glm::quat(camRotation);
		const glm::mat4 modelmat = glm::translate(glm::mat4(1.0f), camPos) * glm::mat4_cast(rotQ);
		const glm::mat4 mvp = glm::perspective(glm::radians(75.0f), (float)window.getSize().x / (float)window.getSize().y, 0.1f, 32.0f) * modelmat;
		memcpy(uniformBuffers[frameIndex].mapped, &mvp, sizeof(glm::mat4));
		// Build CB
		VkCommandBufferBeginInfo cbBI { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, };
		vkResetCommandBuffer(cb, 0);
		vkBeginCommandBuffer(cb, &cbBI);
		VkImageMemoryBarrier barrier0{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = renderImage,
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier0);
		VkRenderingAttachmentInfo colorAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = renderImageView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
			.resolveImageView = swapchainImageViews[imageIndex],
			.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue{.color{ 0.0f, 0.0f, 0.2f, 1.0f }}
		};
		VkRenderingInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea{.extent{.width = window.getSize().x, .height = window.getSize().y }},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp{ .width = static_cast<float>(window.getSize().x), .height = static_cast<float>(window.getSize().y), .minDepth = 0.0f, .maxDepth = 1.0f};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor{ .extent{ .width = window.getSize().x, .height = window.getSize().y } };
		vkCmdSetScissor(cb, 0, 1, &scissor);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffers[frameIndex].descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &texture.descriptorSet, 0, nullptr);
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		VkDeviceSize vOffset{ 0 };
		vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
		vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, 6, 1, 0, 0, 0);
		vkCmdEndRendering(cb);
		VkImageMemoryBarrier barrier1{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = swapchainImages[imageIndex],
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);
		vkEndCommandBuffer(cb);
		// Submit
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &presentSemaphores[frameIndex],
			.pWaitDstStageMask = &waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cb,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &renderSemaphores[imageIndex],
		};
		vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]);
		VkPresentInfoKHR presentInfo{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &renderSemaphores[imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &imageIndex
		};
		chk(vkQueuePresentKHR(queue, &presentInfo));
		frameIndex = (frameIndex + 1) % maxFramesInFlight;
		while (const std::optional event = window.pollEvent())
		{
			if (event->is<sf::Event::Closed>()) {
				window.close();
			}
			if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
				if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
					auto delta = lastMousePos - mouseMoved->position;
					camRotation.x += (float)delta.y * 0.0005f * (float)elapsed.asMilliseconds();
					camRotation.y -= (float)delta.x * 0.0005f * (float)elapsed.asMilliseconds();
				}
				lastMousePos = mouseMoved->position;
			}
			if (const auto* mouseWheelScrolled = event->getIf<sf::Event::MouseWheelScrolled>()) {
				camPos.z += (float)mouseWheelScrolled->delta * 0.025f * (float)elapsed.asMilliseconds();
			}
				vkDeviceWaitIdle(device);
				swapchainCI.oldSwapchain = swapchain;
				swapchainCI.imageExtent = { .width = static_cast<uint32_t>(window.getSize().x), .height = static_cast<uint32_t>(window.getSize().y) };
				chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
				auto oldImageCount = imageCount;
				vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
				swapchainImages.resize(imageCount);
				vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
				vmaDestroyImage(allocator, renderImage, renderImageAllocation);
				vkDestroyImageView(device, renderImageView, nullptr);
				for (auto i = 0; i < swapchainImageViews.size(); i++) {
					vkDestroyImageView(device, swapchainImageViews[i], nullptr);
				}
				swapchainImageViews.resize(imageCount);
				renderImageCI.extent = { .width = static_cast<uint32_t>(window.getSize().x), .height = static_cast<uint32_t>(window.getSize().y), .depth = 1 };
				VmaAllocationCreateInfo allocCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
				chk(vmaCreateImage(allocator, &renderImageCI, &allocCI, &renderImage, &renderImageAllocation, nullptr));
				VkImageViewCreateInfo viewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = renderImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
				chk(vkCreateImageView(device, &viewCI, nullptr, &renderImageView));
				for (auto i = 0; i < imageCount; i++) {
					viewCI.image = swapchainImages[i];
					chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
				}
				vkDestroySwapchainKHR(device, swapchainCI.oldSwapchain, nullptr);
			}
		}
	}
	// Tear down
	vkDeviceWaitIdle(device);
	for (auto i = 0; i < maxFramesInFlight; i++) {
		vkDestroyFence(device, fences[i], nullptr);
		vkDestroySemaphore(device, presentSemaphores[i], nullptr);
		vkDestroySemaphore(device, renderSemaphores[i], nullptr);
		vmaUnmapMemory(allocator, uniformBuffers[i].allocation);
		vmaDestroyBuffer(allocator, uniformBuffers[i].buffer, uniformBuffers[i].allocation);
	}
	vmaDestroyImage(allocator, renderImage, renderImageAllocation);
	vkDestroyImageView(device, renderImageView, nullptr);
	for (auto i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
	vmaDestroyImage(allocator, texture.image, texture.allocation);
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vmaDestroyAllocator(allocator);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
}