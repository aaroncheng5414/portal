#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct GlfwSession {
    GlfwSession() {
        if (!glfwInit()) {
            throw std::runtime_error("Could not initialize GLFW");
        }
    }
    ~GlfwSession() { glfwTerminate(); }
};

using Vec3 = std::array<float, 3>;

struct Vertex {
    Vec3 position;
    Vec3 normal;
};

struct View {
    float scaleX;
    float scaleY;
    float angle;
};

const std::filesystem::path shaderDir = OBJ_SHADER_DIR;

const Vec3& objAttribute(const std::vector<Vec3>& attributes, std::string_view text) {
    int index = 0;
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, index);
    const auto offset = index > 0 ? index - 1 : static_cast<std::ptrdiff_t>(attributes.size()) + index;
    if (parsed.ec != std::errc{} || parsed.ptr != end || index == 0 || offset < 0 ||
        static_cast<size_t>(offset) >= attributes.size()) {
        throw std::runtime_error("Invalid position or normal index in OBJ");
    }
    return attributes[static_cast<size_t>(offset)];
}

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const Vec3 ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const Vec3 n{ab[1] * ac[2] - ab[2] * ac[1],
                 ab[2] * ac[0] - ab[0] * ac[2],
                 ab[0] * ac[1] - ab[1] * ac[0]};
    const float length = std::hypot(n[0], n[1], n[2]);
    if (length == 0.0f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {n[0] / length, n[1] / length, n[2] / length};
}

std::vector<Vertex> loadObj(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open OBJ " + path.u8string());
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vertex> vertices;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "v" || kind == "vn") {
            Vec3 value{};
            if (!(fields >> value[0] >> value[1] >> value[2])) {
                throw std::runtime_error("Invalid position or normal in OBJ");
            }
            (kind == "v" ? positions : normals).push_back(value);
        } else if (kind == "f") {
            std::vector<Vertex> face;
            std::string corner;
            while (fields >> corner && corner.front() != '#') {
                const std::string_view indices(corner);
                const auto firstSlash = indices.find('/');
                const auto lastSlash = indices.rfind('/');
                Vertex vertex{objAttribute(positions, indices.substr(0, firstSlash)), {}};
                if (firstSlash != lastSlash && lastSlash + 1 < indices.size()) {
                    vertex.normal = objAttribute(normals, indices.substr(lastSlash + 1));
                }
                face.push_back(vertex);
            }
            if (face.size() < 3) {
                throw std::runtime_error("Invalid face in OBJ");
            }
            for (size_t i = 2; i < face.size(); ++i) {
                std::array triangle{face[0], face[i - 1], face[i]};
                const auto normal = faceNormal(triangle[0].position, triangle[1].position, triangle[2].position);
                for (auto& vertex : triangle) {
                    if (vertex.normal == Vec3{}) {
                        vertex.normal = normal;
                    }
                    vertices.push_back(vertex);
                }
            }
        }
    }
    if (vertices.empty()) {
        throw std::runtime_error("No triangles found in OBJ");
    }

    Vec3 low = vertices.front().position;
    Vec3 high = low;
    for (const auto& vertex : vertices) {
        for (size_t axis = 0; axis < low.size(); ++axis) {
            low[axis] = std::min(low[axis], vertex.position[axis]);
            high[axis] = std::max(high[axis], vertex.position[axis]);
        }
    }
    float radiusSquared = 0.0f;
    for (auto& vertex : vertices) {
        float distanceSquared = 0.0f;
        for (size_t axis = 0; axis < low.size(); ++axis) {
            vertex.position[axis] -= (low[axis] + high[axis]) * 0.5f;
            distanceSquared += vertex.position[axis] * vertex.position[axis];
        }
        radiusSquared = std::max(radiusSquared, distanceSquared);
    }
    if (radiusSquared == 0.0f) {
        throw std::runtime_error("Degenerate OBJ mesh");
    }
    const float radius = std::sqrt(radiusSquared);
    for (auto& vertex : vertices) {
        for (auto& coordinate : vertex.position) {
            coordinate /= radius;
        }
    }
    return vertices;
}

std::vector<uint32_t> readShader(const char* filename) {
    const auto path = shaderDir / filename;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto bytes = static_cast<std::streamoff>(input.tellg());
    if (bytes <= 0 || bytes % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Could not read shader " + path.string());
    }
    std::vector<uint32_t> words(static_cast<size_t>(bytes) / sizeof(uint32_t));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), bytes) || words.front() != 0x07230203) {
        throw std::runtime_error("Invalid SPIR-V shader " + path.string());
    }
    return words;
}

struct Swapchain {
    vk::UniqueSwapchainKHR handle;
    vk::UniqueDeviceMemory depthMemory;
    vk::UniqueImage depth;
    vk::UniqueImageView depthView;
    vk::UniqueRenderPass renderPass;
    vk::UniquePipeline pipeline;
    std::vector<vk::UniqueImageView> colorViews;
    std::vector<vk::UniqueFramebuffer> framebuffers;
    std::vector<vk::UniqueSemaphore> readyToPresent;
    vk::Extent2D extent;
};

std::optional<vk::SurfaceFormatKHR> chooseFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto format : {vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb,
                              vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm}) {
        for (const auto& supported : formats) {
            if (supported.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
                (supported.format == format || supported.format == vk::Format::eUndefined)) {
                return vk::SurfaceFormatKHR{format, supported.colorSpace};
            }
        }
    }
    return std::nullopt;
}

class MeshRenderer {
public:
    MeshRenderer(GLFWwindow* window, const std::vector<Vertex>& vertices) {
        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions) {
            throw std::runtime_error("GLFW could not find Vulkan window extensions");
        }
        vk::ApplicationInfo app{};
        app.pApplicationName = "P.O.R.T.A.L. OBJ viewer";
        app.apiVersion = VK_API_VERSION_1_0;
        vk::InstanceCreateInfo instanceInfo{};
        instanceInfo.pApplicationInfo = &app;
        instanceInfo.enabledExtensionCount = extensionCount;
        instanceInfo.ppEnabledExtensionNames = extensions;
        instance_ = vk::createInstanceUnique(instanceInfo);

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*instance_, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Could not create the Vulkan window surface");
        }
        surface_ = vk::UniqueSurfaceKHR(vk::SurfaceKHR(surface), instance_.get());
        pickDevice();

        const float priority = 1.0f;
        const std::array queueInfos{
            vk::DeviceQueueCreateInfo({}, graphicsFamily_, 1, &priority),
            vk::DeviceQueueCreateInfo({}, presentFamily_, 1, &priority)
        };
        const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        vk::DeviceCreateInfo deviceInfo{};
        deviceInfo.queueCreateInfoCount = graphicsFamily_ == presentFamily_ ? 1 : 2;
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
        device_ = physicalDevice_.createDeviceUnique(deviceInfo);
        graphicsQueue_ = device_->getQueue(graphicsFamily_, 0);
        presentQueue_ = device_->getQueue(presentFamily_, 0);
        commandPool_ = device_->createCommandPoolUnique(
            {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily_});
        auto commands = device_->allocateCommandBuffersUnique(
            {*commandPool_, vk::CommandBufferLevel::ePrimary, 1});
        command_ = std::move(commands.front());
        imageAvailable_ = device_->createSemaphoreUnique({});
        frameComplete_ = device_->createFenceUnique({vk::FenceCreateFlagBits::eSignaled});

        const vk::PushConstantRange viewRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(View));
        vk::PipelineLayoutCreateInfo pipelineLayout{};
        pipelineLayout.setPushConstantRanges(viewRange);
        pipelineLayout_ = device_->createPipelineLayoutUnique(pipelineLayout);
        vertexShader_ = createShader("mesh.vert.spv");
        fragmentShader_ = createShader("mesh.frag.spv");
        uploadMesh(vertices);
    }

    ~MeshRenderer() {
        vkDeviceWaitIdle(*device_);
    }

    bool resize(int width, int height) {
        const auto caps = physicalDevice_.getSurfaceCapabilitiesKHR(*surface_);
        auto extent = caps.currentExtent;
        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            extent.width = std::clamp(static_cast<uint32_t>(width),
                                      caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(height),
                                       caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) {
            return false;
        }
        const auto format = chooseFormat(physicalDevice_.getSurfaceFormatsKHR(*surface_));
        if (!format || !(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment)) {
            throw std::runtime_error("Vulkan surface does not support color rendering");
        }

        vk::SwapchainCreateInfoKHR info{};
        info.surface = *surface_;
        info.minImageCount = caps.minImageCount;
        if (caps.maxImageCount == 0 || caps.minImageCount < caps.maxImageCount) {
            ++info.minImageCount;
        }
        info.imageFormat = format->format;
        info.imageColorSpace = format->colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        const std::array families{graphicsFamily_, presentFamily_};
        if (graphicsFamily_ != presentFamily_) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.setQueueFamilyIndices(families);
        }
        info.preTransform = caps.currentTransform;
        for (auto alpha : {vk::CompositeAlphaFlagBitsKHR::eOpaque,
                           vk::CompositeAlphaFlagBitsKHR::eInherit,
                           vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                           vk::CompositeAlphaFlagBitsKHR::ePostMultiplied}) {
            if (caps.supportedCompositeAlpha & alpha) {
                info.compositeAlpha = alpha;
                break;
            }
        }
        info.presentMode = vk::PresentModeKHR::eFifo;
        info.clipped = true;

        if (swapchain_) {
            device_->waitIdle();
            info.oldSwapchain = *swapchain_->handle;
        }
        auto next = std::make_unique<Swapchain>();
        next->handle = device_->createSwapchainKHRUnique(info);
        next->extent = extent;
        createDepthAttachment(*next);
        next->renderPass = createRenderPass(format->format);
        next->pipeline = createPipeline(*next->renderPass);
        for (const auto image : device_->getSwapchainImagesKHR(*next->handle)) {
            next->colorViews.push_back(createView(image, format->format, vk::ImageAspectFlagBits::eColor));
            const std::array attachments{*next->colorViews.back(), *next->depthView};
            vk::FramebufferCreateInfo framebuffer{};
            framebuffer.renderPass = *next->renderPass;
            framebuffer.setAttachments(attachments);
            framebuffer.width = extent.width;
            framebuffer.height = extent.height;
            framebuffer.layers = 1;
            next->framebuffers.push_back(device_->createFramebufferUnique(framebuffer));
            next->readyToPresent.push_back(device_->createSemaphoreUnique({}));
        }
        swapchain_ = std::move(next);
        return true;
    }

    bool draw(float angle) {
        auto& chain = *swapchain_;
        if (device_->waitForFences(*frameComplete_, true, UINT64_MAX) != vk::Result::eSuccess) {
            throw std::runtime_error("Could not wait for the Vulkan frame");
        }

        try {
            const auto acquired = device_->acquireNextImageKHR(
                *chain.handle, 100'000'000, *imageAvailable_);
            if (acquired.result == vk::Result::eTimeout) {
                return true;
            }

            recordDraw(chain, acquired.value, angle);

            // Reacquiring this image also makes its previous present semaphore reusable.
            const vk::Semaphore ready = *chain.readyToPresent[acquired.value];
            const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            vk::SubmitInfo submit{};
            submit.setWaitSemaphores(*imageAvailable_);
            submit.setWaitDstStageMask(waitStage);
            submit.setCommandBuffers(*command_);
            submit.setSignalSemaphores(ready);
            device_->resetFences(*frameComplete_);
            graphicsQueue_.submit(submit, *frameComplete_);

            vk::PresentInfoKHR present{};
            present.setWaitSemaphores(ready);
            present.setSwapchains(*chain.handle);
            present.setImageIndices(acquired.value);
            const auto presented = presentQueue_.presentKHR(present);
            return acquired.result != vk::Result::eSuboptimalKHR &&
                   presented != vk::Result::eSuboptimalKHR;
        } catch (const vk::OutOfDateKHRError&) {
            return false;
        }
    }

private:
    void pickDevice() {
        for (const auto device : instance_->enumeratePhysicalDevices()) {
            const auto extensions = device.enumerateDeviceExtensionProperties();
            const bool supportsSwapchain = std::any_of(
                extensions.begin(), extensions.end(), [](const auto& extension) {
                    return std::strcmp(extension.extensionName.data(),
                                       VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                });
            if (!supportsSwapchain) {
                continue;
            }

            const auto queues = device.getQueueFamilyProperties();
            std::optional<uint32_t> graphics;
            std::optional<uint32_t> present;
            for (uint32_t i = 0; i < queues.size(); ++i) {
                if (queues[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                    graphics = i;
                }
                if (device.getSurfaceSupportKHR(i, *surface_)) {
                    present = i;
                }
                if (graphics && present) {
                    break;
                }
            }
            const auto depthFormat = chooseDepthFormat(device);
            if (graphics && present &&
                (device.getSurfaceCapabilitiesKHR(*surface_).supportedUsageFlags &
                 vk::ImageUsageFlagBits::eColorAttachment) &&
                chooseFormat(device.getSurfaceFormatsKHR(*surface_)) && depthFormat) {
                physicalDevice_ = device;
                graphicsFamily_ = *graphics;
                presentFamily_ = *present;
                depthFormat_ = *depthFormat;
                return;
            }
        }
        throw std::runtime_error("No Vulkan device can render and present to this window");
    }

    static std::optional<vk::Format> chooseDepthFormat(vk::PhysicalDevice device) {
        for (const auto format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm}) {
            if (device.getFormatProperties(format).optimalTilingFeatures &
                vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return format;
            }
        }
        return std::nullopt;
    }

    uint32_t memoryType(uint32_t allowedTypes, vk::MemoryPropertyFlags required) const {
        const auto properties = physicalDevice_.getMemoryProperties();
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((allowedTypes & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        throw std::runtime_error("No suitable Vulkan memory type");
    }

    void uploadMesh(const std::vector<Vertex>& vertices) {
        const auto bytes = vertices.size() * sizeof(Vertex);
        vertexBuffer_ = device_->createBufferUnique({{}, bytes, vk::BufferUsageFlagBits::eVertexBuffer});
        const auto requirements = device_->getBufferMemoryRequirements(*vertexBuffer_);
        const auto flags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        vertexMemory_ = device_->allocateMemoryUnique(
            {requirements.size, memoryType(requirements.memoryTypeBits, flags)});
        device_->bindBufferMemory(*vertexBuffer_, *vertexMemory_, 0);
        void* upload = device_->mapMemory(*vertexMemory_, 0, bytes);
        std::memcpy(upload, vertices.data(), bytes);
        device_->unmapMemory(*vertexMemory_);
        vertexCount_ = static_cast<uint32_t>(vertices.size());
    }

    vk::UniqueShaderModule createShader(const char* filename) const {
        const auto words = readShader(filename);
        return device_->createShaderModuleUnique({{}, words.size() * sizeof(uint32_t), words.data()});
    }

    vk::UniqueImageView createView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect) const {
        return device_->createImageViewUnique(
            {{}, image, vk::ImageViewType::e2D, format, {}, {aspect, 0, 1, 0, 1}});
    }

    void createDepthAttachment(Swapchain& chain) const {
        vk::ImageCreateInfo image{};
        image.imageType = vk::ImageType::e2D;
        image.format = depthFormat_;
        image.extent = vk::Extent3D{chain.extent.width, chain.extent.height, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = vk::SampleCountFlagBits::e1;
        image.tiling = vk::ImageTiling::eOptimal;
        image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        chain.depth = device_->createImageUnique(image);
        const auto requirements = device_->getImageMemoryRequirements(*chain.depth);
        chain.depthMemory = device_->allocateMemoryUnique(
            {requirements.size, memoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
        device_->bindImageMemory(*chain.depth, *chain.depthMemory, 0);
        chain.depthView = createView(*chain.depth, depthFormat_, vk::ImageAspectFlagBits::eDepth);
    }

    vk::UniqueRenderPass createRenderPass(vk::Format colorFormat) const {
        const std::array attachments{
            vk::AttachmentDescription({}, colorFormat, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR),
            vk::AttachmentDescription({}, depthFormat_, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal)
        };
        const vk::AttachmentReference color(0, vk::ImageLayout::eColorAttachmentOptimal);
        const vk::AttachmentReference depth(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.setColorAttachments(color);
        subpass.pDepthStencilAttachment = &depth;

        const auto stages = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                            vk::PipelineStageFlagBits::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits::eLateFragmentTests;
        const vk::SubpassDependency dependency(
            VK_SUBPASS_EXTERNAL, 0, stages, stages, {},
            vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
        vk::RenderPassCreateInfo info{};
        info.setAttachments(attachments);
        info.setSubpasses(subpass);
        info.setDependencies(dependency);
        return device_->createRenderPassUnique(info);
    }

    vk::UniquePipeline createPipeline(vk::RenderPass renderPass) const {
        const std::array stages{
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexShader_, "main"),
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentShader_, "main")
        };
        const vk::VertexInputBindingDescription binding(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
        const std::array attributes{
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal))
        };
        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.setVertexBindingDescriptions(binding);
        vertexInput.setVertexAttributeDescriptions(attributes);
        const vk::PipelineInputAssemblyStateCreateInfo assembly({}, vk::PrimitiveTopology::eTriangleList);
        vk::PipelineViewportStateCreateInfo viewport{};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.lineWidth = 1.0f;
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
        vk::PipelineDepthStencilStateCreateInfo depth{};
        depth.depthTestEnable = true;
        depth.depthWriteEnable = true;
        depth.depthCompareOp = vk::CompareOp::eLess;
        vk::PipelineColorBlendAttachmentState color{};
        color.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.setAttachments(color);
        const std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamic{};
        dynamic.setDynamicStates(dynamicStates);

        vk::GraphicsPipelineCreateInfo info{};
        info.setStages(stages);
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = *pipelineLayout_;
        info.renderPass = renderPass;
        return device_->createGraphicsPipelineUnique({}, info).value;
    }

    void recordDraw(const Swapchain& chain, uint32_t imageIndex, float angle) {
        command_->reset();
        command_->begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::Rect2D renderArea({0, 0}, chain.extent);
        const std::array clearValues{
            vk::ClearValue(vk::ClearColorValue(std::array{0.014f, 0.022f, 0.037f, 1.0f})),
            vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
        };
        vk::RenderPassBeginInfo begin{};
        begin.renderPass = *chain.renderPass;
        begin.framebuffer = *chain.framebuffers[imageIndex];
        begin.renderArea = renderArea;
        begin.setClearValues(clearValues);
        command_->beginRenderPass(begin, vk::SubpassContents::eInline);
        command_->bindPipeline(vk::PipelineBindPoint::eGraphics, *chain.pipeline);

        const float width = static_cast<float>(chain.extent.width);
        const float height = static_cast<float>(chain.extent.height);
        command_->setViewport(0, vk::Viewport(0.0f, 0.0f, width, height, 0.0f, 1.0f));
        command_->setScissor(0, renderArea);
        const float scale = 0.86f * std::min(width, height);
        // Positive Vulkan viewport height maps positive clip-space Y down the screen.
        const View view{scale / width, -scale / height, angle};
        command_->pushConstants<View>(*pipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, view);
        command_->bindVertexBuffers(0, *vertexBuffer_, vk::DeviceSize{0});
        command_->draw(vertexCount_, 1, 0, 0);
        command_->endRenderPass();
        command_->end();
    }

    vk::UniqueInstance instance_;
    vk::UniqueSurfaceKHR surface_;
    vk::PhysicalDevice physicalDevice_;
    vk::UniqueDevice device_;
    uint32_t graphicsFamily_ = 0;
    uint32_t presentFamily_ = 0;
    vk::Format depthFormat_ = vk::Format::eUndefined;
    vk::Queue graphicsQueue_;
    vk::Queue presentQueue_;
    vk::UniqueCommandPool commandPool_;
    vk::UniqueCommandBuffer command_;
    vk::UniqueSemaphore imageAvailable_;
    vk::UniqueFence frameComplete_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniqueShaderModule vertexShader_;
    vk::UniqueShaderModule fragmentShader_;
    vk::UniqueDeviceMemory vertexMemory_;
    vk::UniqueBuffer vertexBuffer_;
    uint32_t vertexCount_ = 0;
    std::unique_ptr<Swapchain> swapchain_;
};

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    if (argc != 2) {
        std::cerr << "Usage: main <path-to-model.obj>\n";
        return 1;
    }

    try {
        const auto vertices = loadObj(argv[1]);
        GlfwSession glfw;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(800, 600, "P.O.R.T.A.L. OBJ viewer", nullptr, nullptr),
            glfwDestroyWindow);
        if (!window) {
            throw std::runtime_error("Could not create the GLFW window");
        }

        MeshRenderer renderer(window.get(), vertices);
        int lastWidth = 0;
        int lastHeight = 0;
        bool needsResize = true;
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window.get())) {
                break;
            }

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window.get(), &width, &height);
            if (width == 0 || height == 0) {
                glfwWaitEvents();
                continue;
            }
            if (needsResize || width != lastWidth || height != lastHeight) {
                if (!renderer.resize(width, height)) {
                    glfwWaitEventsTimeout(0.05);
                    continue;
                }
                lastWidth = width;
                lastHeight = height;
            }
            const float angle = 0.35f + static_cast<float>(std::fmod(glfwGetTime() * 0.45, 6.2831853));
            needsResize = !renderer.draw(angle);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
