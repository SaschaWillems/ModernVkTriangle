/* Copyright (c) 2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <SFML/Graphics.hpp>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#include "slang/slang.h"
#include "slang/slang-com-ptr.h"

static inline void chk(VkResult result) {
	if (result != VK_SUCCESS) {
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

static std::string shaderSrc = R"(
struct VSInput
{
	float3 Pos : POSITION0;
	float3 Color;
};
struct VSOutput
{
	float4 Pos : SV_POSITION;
	float3 Color;
};
[shader("vertex")]
VSOutput main(VSInput input)
{
	VSOutput output;
	output.Color = input.Color;
	output.Pos = float4(input.Pos.xyz, 1.0);
	return output;
}
[shader("fragment")]
float4 main(VSOutput input)
{
	return float4(input.Color, 1.0);
})";

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

const uint32_t maxFramesInFlight{ 2 };
const vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
uint32_t imageIndex{ 0 };
uint32_t frameIndex{ 0 };
vk::Result result;
vk::Instance instance;
vk::Device device;
vk::Queue queue;
vk::SurfaceKHR surface{ VK_NULL_HANDLE };
vk::SwapchainKHR swapchain{ VK_NULL_HANDLE };
vk::CommandPool commandPool{ VK_NULL_HANDLE };
vk::Pipeline pipeline;
vk::PipelineLayout pipelineLayout;
vk::Image renderImage;
VmaAllocation renderImageAllocation;
vk::ImageView renderImageView;
std::vector<vk::Image> swapchainImages;
std::vector<vk::ImageView> swapchainImageViews;
std::vector<vk::CommandBuffer> commandBuffers(maxFramesInFlight);
std::vector<vk::Fence> fences(maxFramesInFlight);
std::vector<vk::Semaphore> presentSemaphores(maxFramesInFlight);
std::vector<vk::Semaphore> renderSemaphores(maxFramesInFlight);
VmaAllocator allocator{ VK_NULL_HANDLE };
VmaAllocation vBufferAllocation{ VK_NULL_HANDLE };
vk::Buffer vBuffer{ VK_NULL_HANDLE };
Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;

int main()
{
	// Setup
	auto window = sf::RenderWindow(sf::VideoMode({ 1280, 720u }), "Modern Vulkan Triangle");
	// Initialize slang compiler
	slang::createGlobalSession(slangGlobalSession.writeRef());
	auto targets{ std::to_array<slang::TargetDesc>({ {.format{SLANG_SPIRV}, .profile{slangGlobalSession->findProfile("spirv_1_6")} } }) };
	auto options{ std::to_array<slang::CompilerOptionEntry>({ { slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1} } }) };
	slang::SessionDesc desc{ .targets{targets.data()}, .targetCount{SlangInt(targets.size())}, .compilerOptionEntries{options.data()}, .compilerOptionEntryCount{uint32_t(options.size())} };
	Slang::ComPtr<slang::ISession> slangSession;
	slangGlobalSession->createSession(desc, slangSession.writeRef());
	// Instance
	VULKAN_HPP_DEFAULT_DISPATCHER.init();
	vk::ApplicationInfo appInfo{ .pApplicationName = "Modern Vulkan Triangle", .apiVersion = VK_API_VERSION_1_3 };
	const std::vector<const char*> instanceExtensions{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, };
	vk::InstanceCreateInfo instanceCI{ .pApplicationInfo = &appInfo, .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()), .ppEnabledExtensionNames = instanceExtensions.data() };
	instance = vk::createInstance(instanceCI);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
	// Device
	uint32_t deviceCount{ 0 };
	std::vector<vk::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
	const uint32_t qf{ 0 };
	const float qfpriorities{ 1.0f };
	const uint32_t deviceIndex{ 0 };
	vk::DeviceQueueCreateInfo queueCI{ .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &qfpriorities };
	vk::PhysicalDeviceVulkan13Features features{ .dynamicRendering = true };
	const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	vk::DeviceCreateInfo deviceCI{ .pNext = &features, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueCI, .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()), .ppEnabledExtensionNames = deviceExtensions.data() };
	device = physicalDevices[deviceIndex].createDevice(deviceCI);
	queue = device.getQueue(qf, 0);
	// VMA
	VmaVulkanFunctions vkFunctions{ .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr, .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr, .vkCreateImage = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateImage };
	VmaAllocatorCreateInfo allocatorCI{ .physicalDevice = physicalDevices[deviceIndex], .device = device, .pVulkanFunctions = &vkFunctions, .instance = instance };
	chk(vmaCreateAllocator(&allocatorCI, &allocator));
	// Presentation
	VkSurfaceKHR _surface;
	chk(window.createVulkanSurface(static_cast<VkInstance>(instance), _surface));
	surface = vk::SurfaceKHR(_surface);
	const vk::Format imageFormat{ vk::Format::eB8G8R8A8Srgb };
	vk::SwapchainCreateInfoKHR swapchainCI{
		.surface = surface,
		.minImageCount = 2,
		.imageFormat = imageFormat,
		.imageExtent = {.width = window.getSize().x, .height = window.getSize().y, },
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		.presentMode = vk::PresentModeKHR::eFifo,
	};
	swapchain = device.createSwapchainKHR(swapchainCI);
	swapchainImages = device.getSwapchainImagesKHR(swapchain);
	vk::ImageCreateInfo renderImageCI{ .imageType = vk::ImageType::e2D, .format = imageFormat, .extent = {.width = window.getSize().x, .height = window.getSize().y, .depth = 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = sampleCount, .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc };
	VmaAllocationCreateInfo allocCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f };
	vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&renderImageCI), &allocCI, reinterpret_cast<VkImage*>(&renderImage), &renderImageAllocation, nullptr);
	vk::ImageViewCreateInfo viewCI{ .image = renderImage, .viewType = vk::ImageViewType::e2D, .format = imageFormat, .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
	renderImageView = device.createImageView(viewCI);
	swapchainImageViews.resize(swapchainImages.size());
	for (auto i = 0; i < swapchainImages.size(); i++) {
		viewCI.image = swapchainImages[i];
		swapchainImageViews[i] = device.createImageView(viewCI);
	}
	// Vertexbuffer (Pos 3f, Col 3f)
	const std::vector<float> vertices{ 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, /**/ 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, /**/ -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f };
	VkBufferCreateInfo bufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(float) * vertices.size(), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
	VmaAllocationCreateInfo bufferAllocCI{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	VmaAllocationInfo bufferAllocInfo{};
	chk(vmaCreateBuffer(allocator, &bufferCI, &bufferAllocCI, reinterpret_cast<VkBuffer*>(&vBuffer), &vBufferAllocation, &bufferAllocInfo));
	memcpy(bufferAllocInfo.pMappedData, vertices.data(), sizeof(float) * vertices.size());
	commandPool = device.createCommandPool({ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = qf });
	// Sync objects
	commandBuffers = device.allocateCommandBuffers({ .commandPool = commandPool, .commandBufferCount = maxFramesInFlight });
	for (auto i = 0; i < maxFramesInFlight; i++) {
		fences[i] = device.createFence({ .flags = vk::FenceCreateFlagBits::eSignaled });
		presentSemaphores[i] = device.createSemaphore({});
		renderSemaphores[i] = device.createSemaphore({});
	}
	// Shaders	
	Slang::ComPtr<slang::IModule> slangModule{ slangSession->loadModuleFromSourceString("triangle", nullptr, shaderSrc.c_str()) };
	Slang::ComPtr<ISlangBlob> spirv;
	slangModule->getTargetCode(0, spirv.writeRef());
	vk::ShaderModule shaderModule = device.createShaderModule({ .codeSize = spirv->getBufferSize(), .pCode = (uint32_t*)spirv->getBufferPointer() });
	std::vector<vk::PipelineShaderStageCreateInfo> stages{
		{.stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "main"},
		{.stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "main" }
	};
	// Pipeline
	pipelineLayout = device.createPipelineLayout({});
	vk::VertexInputBindingDescription vertexBinding{ .binding = 0, .stride = sizeof(float) * 6, .inputRate = vk::VertexInputRate::eVertex };
	std::vector<vk::VertexInputAttributeDescription> vertexAttributes{
		{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat },
		{.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = sizeof(float) * 3},
	};
	vk::PipelineVertexInputStateCreateInfo vertexInputState{ .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertexBinding, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = vertexAttributes.data(), };
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{ .topology = vk::PrimitiveTopology::eTriangleList };
	vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
	vk::PipelineRasterizationStateCreateInfo rasterizationState{ .lineWidth = 1.0f };
	vk::PipelineMultisampleStateCreateInfo multisampleState{ .rasterizationSamples = sampleCount };
	vk::PipelineDepthStencilStateCreateInfo depthStencilState{ };
	vk::PipelineColorBlendAttachmentState blendAttachment{ .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA };
	vk::PipelineColorBlendStateCreateInfo colorBlendState{ .attachmentCount = 1, .pAttachments = &blendAttachment };
	std::vector<vk::DynamicState> dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynamicStates.data() };
	vk::PipelineRenderingCreateInfo renderingCI{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &imageFormat };
	vk::GraphicsPipelineCreateInfo pipelineCI{
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
	std::tie(result, pipeline) = device.createGraphicsPipeline(nullptr, pipelineCI);
	device.destroyShaderModule(shaderModule, nullptr);
	// Render
	while (window.isOpen())
	{
		// Build CB
		device.waitForFences(fences[frameIndex], true, UINT64_MAX);
		device.resetFences(fences[frameIndex]);
		device.acquireNextImageKHR(swapchain, UINT64_MAX, presentSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
		auto& cb = commandBuffers[frameIndex];
		cb.reset();
		cb.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		vk::ImageMemoryBarrier barrier0{
			.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eGeneral,
			.image = renderImage,
			.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
		};
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags{ 0 }, nullptr, nullptr, barrier0);
		vk::RenderingAttachmentInfo colorAttachmentInfo{
			.imageView = renderImageView,
			.imageLayout = vk::ImageLayout::eGeneral,
			.resolveMode = vk::ResolveModeFlagBits::eAverage,
			.resolveImageView = swapchainImageViews[imageIndex],
			.resolveImageLayout = vk::ImageLayout::eGeneral,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = vk::ClearValue{ vk::ClearColorValue{ std::array<float, 4>{0.0f, 0.0f, 0.2f, 1.0f} } },
		};
		vk::RenderingInfo renderingInfo{ .renderArea = {.extent = {.width = window.getSize().x, .height = window.getSize().y }}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentInfo };
		cb.beginRendering(renderingInfo);
		vk::Viewport vp{ .width = static_cast<float>(window.getSize().x), .height = static_cast<float>(window.getSize().y), .minDepth = 0.0f, .maxDepth = 1.0f };
		cb.setViewport(0, 1, &vp);
		cb.setScissor(0, 1, &renderingInfo.renderArea);
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		vk::DeviceSize vOffset{ 0 };
		cb.bindVertexBuffers(0, 1, &vBuffer, &vOffset);
		cb.draw(6, 1, 0, 0);
		cb.endRendering();
		vk::ImageMemoryBarrier barrier1{
			.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::ePresentSrcKHR,
			.image = swapchainImages[imageIndex],
			.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 }
		};
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags{ 0 }, nullptr, nullptr, barrier1);
		cb.end();
		// Submit
		vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &presentSemaphores[frameIndex],
			.pWaitDstStageMask = &waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cb,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &renderSemaphores[frameIndex],
		};
		queue.submit(submitInfo, fences[frameIndex]);
		queue.presentKHR({ .waitSemaphoreCount = 1, .pWaitSemaphores = &renderSemaphores[frameIndex], .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &imageIndex });
		frameIndex++;
		if (frameIndex >= maxFramesInFlight) { frameIndex = 0; }
		while (const std::optional event = window.pollEvent()) {
			if (event->is<sf::Event::Closed>()) {
				window.close();
			}
			if (event->is<sf::Event::Resized>()) {
				device.waitIdle();
				swapchainCI.oldSwapchain = swapchain;
				swapchainCI.imageExtent = { .width = static_cast<uint32_t>(window.getSize().x), .height = static_cast<uint32_t>(window.getSize().y) };
				swapchain = device.createSwapchainKHR(swapchainCI);
				swapchainImages = device.getSwapchainImagesKHR(swapchain);
				vmaDestroyImage(allocator, renderImage, renderImageAllocation);
				device.destroyImageView(renderImageView, nullptr);
				for (auto i = 0; i < swapchainImageViews.size(); i++) {
					device.destroyImageView(swapchainImageViews[i], nullptr);
				}
				swapchainImageViews.resize(swapchainImages.size());
				renderImageCI.extent = { .width = static_cast<uint32_t>(window.getSize().x), .height = static_cast<uint32_t>(window.getSize().y), .depth = 1 };
				VmaAllocationCreateInfo allocCI = { .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f };
				chk(vmaCreateImage(allocator, reinterpret_cast<VkImageCreateInfo*>(&renderImageCI), &allocCI, reinterpret_cast<VkImage*>(&renderImage), &renderImageAllocation, nullptr));
				vk::ImageViewCreateInfo viewCI = { .image = renderImage, .viewType = vk::ImageViewType::e2D, .format = imageFormat, .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 } };
				renderImageView = device.createImageView(viewCI);
				for (auto i = 0; i < swapchainImages.size(); i++) {
					viewCI.image = swapchainImages[i];
					swapchainImageViews[i] = device.createImageView(viewCI);
				}
				device.destroySwapchainKHR(swapchainCI.oldSwapchain, nullptr);
			}
		}
	}
	// Tear down
	device.waitIdle();
	for (auto i = 0; i < maxFramesInFlight; i++) {
		device.destroyFence(fences[i], nullptr);
		device.destroySemaphore(presentSemaphores[i], nullptr);
		device.destroySemaphore(renderSemaphores[i], nullptr);
	}
	vmaDestroyImage(allocator, renderImage, renderImageAllocation);
	device.destroyImageView(renderImageView, nullptr);
	for (auto i = 0; i < swapchainImageViews.size(); i++) {
		device.destroyImageView(swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
	device.destroyCommandPool(commandPool, nullptr);
	device.destroyPipelineLayout(pipelineLayout, nullptr);
	device.destroyPipeline(pipeline, nullptr);
	device.destroySwapchainKHR(swapchain, nullptr);
	vmaDestroyAllocator(allocator);
	device.destroy();
	instance.destroySurfaceKHR(surface, nullptr);
	instance.destroy();
}