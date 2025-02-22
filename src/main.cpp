/* Copyright (c) 2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <SFML/Graphics.hpp>
#define VOLK_IMPLEMENTATION
#include <volk/volk.h>
#include <vector>
#include <string>
#include <iostream>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#include <atlbase.h>
#include "dxcapi.h"

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
static inline void chk(HRESULT result) {
	if (FAILED(result)) {
		std::cerr << "Call returned an error\n";
		exit(result);
	}
}

static std::string vertShader = R"(
struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Color : COLOR0;
};
struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Color : COLOR0;
};
VSOutput main(VSInput input)
{
	VSOutput output;
	output.Color = input.Color;
	output.Pos = float4(input.Pos.xyz, 1.0);
	return output;
})";
static std::string fragShader = R"(
float4 main([[vk::location(0)]] float3 Color : COLOR0) : SV_TARGET
{
	return float4(Color, 1.0);
})";

const uint32_t maxFramesInFlight{ 2 };
const VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT;
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
std::vector<VkCommandBuffer> commandBuffers(maxFramesInFlight);
std::vector<VkFence> fences(maxFramesInFlight);
std::vector<VkSemaphore> presentSemaphores(maxFramesInFlight);
std::vector<VkSemaphore> renderSemaphores(maxFramesInFlight);
VmaAllocator allocator{ VK_NULL_HANDLE };
VmaAllocation vBufferAllocation{ VK_NULL_HANDLE };
VkBuffer vBuffer{ VK_NULL_HANDLE };
CComPtr<IDxcLibrary> library{ nullptr };
CComPtr<IDxcCompiler3> compiler{ nullptr };
CComPtr<IDxcUtils> utils{ nullptr };

static VkPipelineShaderStageCreateInfo compileShader(std::string& shader, VkShaderStageFlagBits shaderStage) {
	LPCWSTR targetProfile{};
	switch (shaderStage) {
	case VK_SHADER_STAGE_FRAGMENT_BIT:
		targetProfile = L"ps_6_1";
		break;
	default:
		targetProfile = L"vs_6_1";
	}
	std::vector<LPCWSTR> arguments = { L"-E", L"main",  L"-T", targetProfile,  L"-spirv"};
	DxcBuffer buffer{};
	buffer.Encoding = DXC_CP_ACP;
	buffer.Ptr = shader.c_str();
	buffer.Size = shader.size();
	CComPtr<IDxcResult> result{ nullptr };
	chk(compiler->Compile(&buffer, arguments.data(), (uint32_t)arguments.size(), nullptr, IID_PPV_ARGS(&result)));
	CComPtr<IDxcBlob> code;
	result->GetResult(&code);
	VkShaderModuleCreateInfo shaderModuleCI = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code->GetBufferSize(), .pCode = (uint32_t*)code->GetBufferPointer() };
	VkShaderModule shaderModule;
	vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule);
	return { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = shaderStage, .module = shaderModule, .pName = "main" };
};

int main()
{
	// Setup
	auto window = sf::RenderWindow(sf::VideoMode({ 1280, 720u }), "Modern Vulkan Triangle");
	volkInitialize();
	// Initialize DXC compiler
	chk(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)));
	chk(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
	chk(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
	// Instance
	VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Modern Vulkan Triangle", .apiVersion = VK_API_VERSION_1_3 };
	const std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, };
	VkInstanceCreateInfo instanceCI = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
		.ppEnabledExtensionNames = instanceExtensions.data(),
	};
	chk(vkCreateInstance(&instanceCI, nullptr, &instance));
	volkLoadInstance(instance);
	// Device
	uint32_t deviceCount{ 0 };
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	const uint32_t qf{ 0 };
	const float qfpriorities{ 1.0f };
	const uint32_t deviceIndex{ 0 };
	VkDeviceQueueCreateInfo queueCI = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &qfpriorities };
	VkPhysicalDeviceVulkan13Features features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = true };
	const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceCI = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCI,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
	};
	chk(vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device));
	vkGetDeviceQueue(device, qf, 0, &queue);
	// VMA
	VmaVulkanFunctions vkFunctions = { .vkGetInstanceProcAddr = vkGetInstanceProcAddr, .vkGetDeviceProcAddr = vkGetDeviceProcAddr, .vkCreateImage = vkCreateImage };
	VmaAllocatorCreateInfo allocatorCI = { .physicalDevice = devices[deviceIndex], .device = device, .pVulkanFunctions = &vkFunctions, .instance = instance };
	chk(vmaCreateAllocator(&allocatorCI, &allocator));
	// Presentation
	chk(window.createVulkanSurface(instance, surface));
	const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	const VkColorSpaceKHR colorSpace{ VK_COLORSPACE_SRGB_NONLINEAR_KHR };
	VkSwapchainCreateInfoKHR swapchainCI = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = 2,
		.imageFormat = imageFormat,
		.imageColorSpace = colorSpace,
		.imageExtent = { .width = window.getSize().x, .height = window.getSize().y, },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.queueFamilyIndexCount = qf,
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
	VkImageCreateInfo renderImageCI = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imageFormat,
		.extent = {.width = window.getSize().x, .height = window.getSize().y, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = sampleCount,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCI = { .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f };
	vmaCreateImage(allocator, &renderImageCI, &allocCI, &renderImage, &renderImageAllocation, nullptr);
	VkImageViewCreateInfo viewCI = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = renderImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
	chk(vkCreateImageView(device, &viewCI, nullptr, &renderImageView));
	for (auto i = 0; i < imageCount; i++) {
		viewCI.image = swapchainImages[i];
		chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
	}
	// Vertexbuffer (Pos 3f, Col 3f)
	const std::vector<float> vertices = { 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, /**/ 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, /**/ -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f };
	VkBufferCreateInfo bufferCI = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(float) * vertices.size(), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
	VmaAllocationCreateInfo bufferAllocCI = { .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	chk(vmaCreateBuffer(allocator, &bufferCI, &bufferAllocCI, &vBuffer, &vBufferAllocation, nullptr));
	void* bufferPtr{ nullptr };
	vmaMapMemory(allocator, vBufferAllocation, &bufferPtr);
	memcpy(bufferPtr, vertices.data(), sizeof(float)* vertices.size());
	vmaUnmapMemory(allocator, vBufferAllocation);
	VkCommandPoolCreateInfo commandPoolCI = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = qf };
	chk(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
	// Sync objects
	for (auto i = 0; i < maxFramesInFlight; i++) {
		VkCommandBufferAllocateInfo cbAllocCI = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .commandBufferCount = 1};
		chk(vkAllocateCommandBuffers(device, &cbAllocCI, &commandBuffers[i]));
		VkFenceCreateInfo fenceCI = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		vkCreateFence(device, &fenceCI, nullptr, &fences[i]);
		VkSemaphoreCreateInfo semaphoreCI = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderSemaphores[i]));
	}
	// Pipeline
	VkPipelineLayoutCreateInfo pipelineLayoutCI = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	chk(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
	std::vector<VkPipelineShaderStageCreateInfo> stages = { compileShader(vertShader, VK_SHADER_STAGE_VERTEX_BIT), compileShader(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT) };
	VkVertexInputBindingDescription vertexBinding = { .binding = 0, .stride = sizeof(float) * 6, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> vertexAttributes = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = sizeof(float) * 3},
	};
	VkPipelineVertexInputStateCreateInfo vertexInputState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertexBinding,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = vertexAttributes.data(),
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
	VkPipelineRasterizationStateCreateInfo rasterizationState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampleState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = sampleCount };
	VkPipelineDepthStencilStateCreateInfo depthStencilState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState blendAttachment = { .colorWriteMask = 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlendState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment };
	std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates.data() };
	VkPipelineRenderingCreateInfo renderingCI = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &imageFormat };
	VkGraphicsPipelineCreateInfo pipelineCI = {
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
		.layout = pipelineLayout,
	};
	chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));
	vkDestroyShaderModule(device, stages[0].module, nullptr);
	vkDestroyShaderModule(device, stages[1].module, nullptr);
	// Render
	while (window.isOpen())
	{
		// Build CB
		vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX);
		vkResetFences(device, 1, &fences[frameIndex]);
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
		auto cb = commandBuffers[frameIndex];
		VkCommandBufferBeginInfo cbBI { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, };
		vkResetCommandBuffer(cb, 0);
		vkBeginCommandBuffer(cb, &cbBI);
		VkImageMemoryBarrier barrier0 = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = renderImage,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier0);
		VkRenderingAttachmentInfo colorAttachmentInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = renderImageView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
			.resolveImageView = swapchainImageViews[imageIndex],
			.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.color = { 0.0f, 0.0f, 0.2f, 1.0f }}
		};
		VkRenderingInfo renderingInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = {.extent = {.width = window.getSize().x, .height = window.getSize().y }},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp = { .width = static_cast<float>(window.getSize().x), .height = static_cast<float>(window.getSize().y), .minDepth = 0.0f, .maxDepth = 1.0f};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor  = { .extent = { .width = window.getSize().x, .height = window.getSize().y } };
		vkCmdSetScissor(cb, 0, 1, &scissor);
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		VkDeviceSize vOffset{ 0 };
		vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
		vkCmdDraw(cb, 6, 1, 0, 0);
		vkCmdEndRendering(cb);
		VkImageMemoryBarrier barrier1 = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = swapchainImages[imageIndex],
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);
		vkEndCommandBuffer(cb);
		// Submit
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &presentSemaphores[frameIndex],
			.pWaitDstStageMask = &waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cb,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &renderSemaphores[frameIndex],
		};
		vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]);
		VkPresentInfoKHR presentInfo = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &renderSemaphores[frameIndex],
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &imageIndex
		};
		chk(vkQueuePresentKHR(queue, &presentInfo));
		frameIndex++;
		if (frameIndex >= maxFramesInFlight) { frameIndex = 0; }
		while (const std::optional event = window.pollEvent())
		{
			if (event->is<sf::Event::Closed>())
			{
				window.close();
			}
			if (event->is<sf::Event::Resized>())
			{
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
				VmaAllocationCreateInfo allocCI = { .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f };
				chk(vmaCreateImage(allocator, &renderImageCI, &allocCI, &renderImage, &renderImageAllocation, nullptr));
				VkImageViewCreateInfo viewCI = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = renderImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
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
	}
	vmaDestroyImage(allocator, renderImage, renderImageAllocation);
	vkDestroyImageView(device, renderImageView, nullptr);
	for (auto i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vmaDestroyAllocator(allocator);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
}