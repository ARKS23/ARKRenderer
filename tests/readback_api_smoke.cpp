#include "core/Memory.h"
#include "renderer/resources/EnvironmentCubeResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/Fence.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
    class FakeBuffer final : public ark::rhi::Buffer {
    public:
        explicit FakeBuffer(const ark::rhi::BufferDesc& desc) : m_Desc(desc), m_Data(static_cast<ark::usize>(desc.size)) {
            if (desc.initialData && !m_Data.empty()) {
                std::memcpy(m_Data.data(), desc.initialData, m_Data.size());
            }
        }

        const ark::rhi::BufferDesc& getDesc() const override {
            return m_Desc;
        }

        bool readData(void* destination, ark::u64 size, ark::u64 offset = 0) const override {
            if (!destination || size == 0 ||
                m_Desc.memoryUsage != ark::rhi::MemoryUsage::GpuToCpu ||
                offset > m_Desc.size ||
                size > m_Desc.size - offset) {
                return false;
            }

            std::memcpy(destination, m_Data.data() + offset, static_cast<ark::usize>(size));
            return true;
        }

        void writeBytes(ark::u64 offset, const void* data, ark::u64 size) {
            if (!data || offset > m_Desc.size || size > m_Desc.size - offset) {
                return;
            }

            std::memcpy(m_Data.data() + offset, data, static_cast<ark::usize>(size));
        }

    private:
        ark::rhi::BufferDesc m_Desc;
        std::vector<ark::u8> m_Data;
    };

    class FakeTexture final : public ark::rhi::Texture {
    public:
        explicit FakeTexture(const ark::rhi::TextureDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::TextureDesc& getDesc() const override {
            return m_Desc;
        }

        ark::rhi::ResourceState getState() const override {
            return m_State;
        }

        void setState(ark::rhi::ResourceState state) {
            m_State = state;
        }

    private:
        ark::rhi::TextureDesc m_Desc;
        ark::rhi::ResourceState m_State = ark::rhi::ResourceState::ShaderResource;
    };

    class FakeTextureView final : public ark::rhi::TextureView {
    public:
        FakeTextureView(ark::rhi::Texture& texture, const ark::rhi::TextureViewDesc& desc)
            : m_Texture(&texture), m_Desc(desc) {
        }

        ark::rhi::Texture* getTexture() const override {
            return m_Texture;
        }

        const ark::rhi::TextureViewDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::Texture* m_Texture = nullptr;
        ark::rhi::TextureViewDesc m_Desc;
    };

    class FakeSampler final : public ark::rhi::Sampler {
    public:
        explicit FakeSampler(const ark::rhi::SamplerDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::SamplerDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::SamplerDesc m_Desc;
    };

    class FakeShader final : public ark::rhi::Shader {
    public:
        const ark::rhi::ShaderDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::ShaderDesc m_Desc;
    };

    class FakePipelineLayout final : public ark::rhi::PipelineLayout {
    public:
        const ark::rhi::PipelineLayoutDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::PipelineLayoutDesc m_Desc;
    };

    class FakePipelineState final : public ark::rhi::PipelineState {
    public:
        const ark::rhi::GraphicsPipelineDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::GraphicsPipelineDesc m_Desc;
    };

    class FakeDescriptorSetLayout final : public ark::rhi::DescriptorSetLayout {
    public:
        const ark::rhi::DescriptorSetLayoutDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::DescriptorSetLayoutDesc m_Desc;
    };

    class FakeDescriptorSet final : public ark::rhi::DescriptorSet {
    public:
        void updateUniformBuffer(ark::u32, const ark::rhi::BufferDescriptor&) override {
        }

        void updateSampledImage(ark::u32, const ark::rhi::SampledImageDescriptor&) override {
        }

        void updateSampler(ark::u32, const ark::rhi::SamplerDescriptor&) override {
        }
    };

    class FakeFence final : public ark::rhi::Fence {
    };

    class FakeRenderDevice final : public ark::rhi::RenderDevice {
    public:
        void waitIdle() override {
            ++waitIdleCount;
        }

        ark::rhi::RenderBackendType getBackendType() const override {
            return ark::rhi::RenderBackendType::Vulkan;
        }

        const ark::rhi::RenderDeviceCaps& getCaps() const override {
            return m_Caps;
        }

        ark::Scope<ark::rhi::Buffer> createBuffer(const ark::rhi::BufferDesc& desc) override {
            bufferDescs.push_back(desc);
            return ark::makeScope<FakeBuffer>(desc);
        }

        ark::Scope<ark::rhi::Texture> createTexture(const ark::rhi::TextureDesc& desc) override {
            textureDescs.push_back(desc);
            return ark::makeScope<FakeTexture>(desc);
        }

        ark::Scope<ark::rhi::TextureView> createTextureView(ark::rhi::Texture& texture,
                                                            const ark::rhi::TextureViewDesc& desc) override {
            textureViewDescs.push_back(desc);
            return ark::makeScope<FakeTextureView>(texture, desc);
        }

        ark::Scope<ark::rhi::Sampler> createSampler(const ark::rhi::SamplerDesc& desc) override {
            return ark::makeScope<FakeSampler>(desc);
        }

        ark::Scope<ark::rhi::Shader> createShader(const ark::rhi::ShaderDesc&) override {
            return ark::makeScope<FakeShader>();
        }

        ark::Scope<ark::rhi::PipelineLayout> createPipelineLayout(const ark::rhi::PipelineLayoutDesc&) override {
            return ark::makeScope<FakePipelineLayout>();
        }

        ark::Scope<ark::rhi::PipelineState> createGraphicsPipeline(const ark::rhi::GraphicsPipelineDesc&) override {
            return ark::makeScope<FakePipelineState>();
        }

        ark::Scope<ark::rhi::DescriptorSetLayout>
        createDescriptorSetLayout(const ark::rhi::DescriptorSetLayoutDesc&) override {
            return ark::makeScope<FakeDescriptorSetLayout>();
        }

        ark::Scope<ark::rhi::DescriptorSet> createDescriptorSet(const ark::rhi::DescriptorSetLayout&) override {
            return ark::makeScope<FakeDescriptorSet>();
        }

        ark::Scope<ark::rhi::Fence> createFence() override {
            return ark::makeScope<FakeFence>();
        }

        std::vector<ark::rhi::BufferDesc> bufferDescs;
        std::vector<ark::rhi::TextureDesc> textureDescs;
        std::vector<ark::rhi::TextureViewDesc> textureViewDescs;
        int waitIdleCount = 0;

    private:
        ark::rhi::RenderDeviceCaps m_Caps{};
    };

    class FakeDeviceContext final : public ark::rhi::DeviceContext {
    public:
        ark::rhi::FrameResource& beginFrame() override {
            return frame;
        }

        bool begin(ark::rhi::FrameResource&) override {
            recording = true;
            return true;
        }

        bool end() override {
            recording = false;
            return true;
        }

        bool submit(const ark::rhi::SubmitDesc&) override {
            return true;
        }

        void advanceFrame() override {
        }

        bool beginRendering(const ark::rhi::RenderingDesc&) override {
            rendering = true;
            return true;
        }

        void endRendering() override {
            rendering = false;
        }

        void setViewport(const ark::rhi::Viewport&) override {
        }

        void setScissorRect(const ark::rhi::ScissorRect&) override {
        }

        void setPipeline(ark::rhi::PipelineState&) override {
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
        }

        bool updateBuffer(ark::rhi::Buffer&, const void*, ark::u64, ark::u64 = 0) override {
            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc&) override {
            return true;
        }

        bool copyTextureToBuffer(const ark::rhi::TextureReadbackDesc& desc) override {
            if (!recording || rendering || !desc.texture || !desc.destinationBuffer ||
                !ark::rhi::isValidExtent(desc.extent)) {
                return false;
            }

            if (!ark::rhi::hasTextureUsage(desc.texture->getDesc().usage, ark::rhi::TextureUsage::TransferSrc) ||
                !ark::rhi::hasBufferUsage(desc.destinationBuffer->getDesc().usage, ark::rhi::BufferUsage::TransferDst) ||
                desc.destinationBuffer->getDesc().memoryUsage != ark::rhi::MemoryUsage::GpuToCpu) {
                return false;
            }

            if (desc.texture->getState() != ark::rhi::ResourceState::CopySrc) {
                return false;
            }

            copyDescs.push_back(desc);
            return true;
        }

        bool generateTextureMips(ark::rhi::Texture&) override {
            return true;
        }

        bool uploadBufferData(const ark::rhi::BufferUploadDesc&) override {
            return true;
        }

        bool deferReleaseBuffer(ark::Scope<ark::rhi::Buffer>& buffer) override {
            buffer.reset();
            return true;
        }

        bool deferReleaseTexture(ark::Scope<ark::rhi::Texture>& texture) override {
            texture.reset();
            return true;
        }

        bool deferReleaseTextureView(ark::Scope<ark::rhi::TextureView>& textureView) override {
            textureView.reset();
            return true;
        }

        bool deferReleaseSampler(ark::Scope<ark::rhi::Sampler>& sampler) override {
            sampler.reset();
            return true;
        }

        void setVertexBuffer(ark::u32, ark::rhi::Buffer&, ark::u64 = 0) override {
        }

        void setIndexBuffer(ark::rhi::Buffer&, ark::rhi::IndexType = ark::rhi::IndexType::UInt32, ark::u64 = 0) override {
        }

        void draw(const ark::rhi::DrawDesc&) override {
        }

        void drawIndexed(const ark::rhi::DrawIndexedDesc&) override {
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier> barriers) override {
            for (const ark::rhi::ResourceBarrier& barrier : barriers) {
                if (auto* texture = dynamic_cast<FakeTexture*>(barrier.texture)) {
                    texture->setState(barrier.after);
                }
            }
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
        std::vector<ark::rhi::TextureReadbackDesc> copyDescs;
        bool recording = false;
        bool rendering = false;
    };

    bool validateReadbackBuffer() {
        const ark::u32 expected = 0xdecafbad;

        ark::rhi::BufferDesc readbackDesc{};
        readbackDesc.debugName = "ReadbackBuffer";
        readbackDesc.size = sizeof(expected);
        readbackDesc.usage = ark::rhi::BufferUsage::TransferDst;
        readbackDesc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;

        FakeBuffer readbackBuffer{readbackDesc};
        readbackBuffer.writeBytes(0, &expected, sizeof(expected));

        ark::u32 actual = 0;
        if (!readbackBuffer.readData(&actual, sizeof(actual)) || actual != expected) {
            std::cerr << "GpuToCpu buffer readData did not return expected bytes\n";
            return false;
        }

        if (readbackBuffer.readData(nullptr, sizeof(actual)) ||
            readbackBuffer.readData(&actual, 0) ||
            readbackBuffer.readData(&actual, sizeof(actual), readbackDesc.size)) {
            std::cerr << "GpuToCpu buffer readData accepted invalid ranges\n";
            return false;
        }

        ark::rhi::BufferDesc gpuOnlyDesc = readbackDesc;
        gpuOnlyDesc.memoryUsage = ark::rhi::MemoryUsage::GpuOnly;
        FakeBuffer gpuOnlyBuffer{gpuOnlyDesc};
        if (gpuOnlyBuffer.readData(&actual, sizeof(actual))) {
            std::cerr << "GpuOnly buffer unexpectedly allowed CPU readback\n";
            return false;
        }

        return true;
    }

    bool validateCopyTextureToBufferContract() {
        FakeDeviceContext context{};

        ark::rhi::TextureDesc textureDesc{};
        textureDesc.extent = ark::rhi::Extent2D{4, 4};
        textureDesc.format = ark::rhi::Format::RGBA16Float;
        textureDesc.usage = ark::rhi::TextureUsage::ShaderResource | ark::rhi::TextureUsage::TransferSrc;
        FakeTexture texture{textureDesc};

        ark::rhi::BufferDesc readbackDesc{};
        readbackDesc.debugName = "TextureReadback";
        readbackDesc.size = 4 * 4 * 8;
        readbackDesc.usage = ark::rhi::BufferUsage::TransferDst;
        readbackDesc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;
        FakeBuffer readbackBuffer{readbackDesc};

        ark::rhi::TextureReadbackDesc readback{};
        readback.texture = &texture;
        readback.destinationBuffer = &readbackBuffer;
        readback.extent = textureDesc.extent;
        readback.bytesPerPixel = 8;

        if (context.copyTextureToBuffer(readback)) {
            std::cerr << "copyTextureToBuffer accepted copy before command recording\n";
            return false;
        }

        context.begin(context.frame);
        if (context.copyTextureToBuffer(readback)) {
            std::cerr << "copyTextureToBuffer accepted texture before CopySrc state\n";
            return false;
        }

        const ark::rhi::ResourceBarrier toCopySrc{&texture,
                                                  texture.getState(),
                                                  ark::rhi::ResourceState::CopySrc};
        context.pipelineBarrier(std::span<const ark::rhi::ResourceBarrier>(&toCopySrc, 1));
        if (!context.copyTextureToBuffer(readback)) {
            std::cerr << "copyTextureToBuffer rejected valid readback desc\n";
            return false;
        }

        context.beginRendering(ark::rhi::RenderingDesc{});
        if (context.copyTextureToBuffer(readback)) {
            std::cerr << "copyTextureToBuffer accepted copy during rendering\n";
            return false;
        }
        context.endRendering();
        context.end();

        if (context.copyDescs.size() != 1 ||
            context.copyDescs.front().texture != &texture ||
            context.copyDescs.front().destinationBuffer != &readbackBuffer ||
            context.copyDescs.front().extent.width != 4 ||
            context.copyDescs.front().extent.height != 4 ||
            context.copyDescs.front().bytesPerPixel != 8) {
            std::cerr << "copyTextureToBuffer did not record expected descriptor\n";
            return false;
        }

        return true;
    }

    bool validateEnvironmentCubeReadbackUsage() {
        FakeRenderDevice device{};

        ark::EnvironmentCubeResourceDesc defaultDesc{};
        defaultDesc.debugName = "DefaultCube";
        defaultDesc.faceExtent = ark::rhi::Extent2D{16, 16};
        defaultDesc.format = ark::rhi::Format::RGBA32Float;

        ark::EnvironmentCubeResource defaultCube{};
        if (!defaultCube.create(device, defaultDesc)) {
            std::cerr << "Default EnvironmentCubeResource create failed\n";
            return false;
        }

        if (device.textureDescs.empty() ||
            ark::rhi::hasTextureUsage(device.textureDescs.back().usage, ark::rhi::TextureUsage::TransferSrc)) {
            std::cerr << "Default EnvironmentCubeResource unexpectedly enabled readback usage\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc readbackDesc = defaultDesc;
        readbackDesc.debugName = "ReadbackCube";
        readbackDesc.allowReadback = true;

        ark::EnvironmentCubeResource readbackCube{};
        if (!readbackCube.create(device, readbackDesc)) {
            std::cerr << "Readback EnvironmentCubeResource create failed\n";
            return false;
        }

        if (!ark::rhi::hasTextureUsage(device.textureDescs.back().usage, ark::rhi::TextureUsage::TransferSrc)) {
            std::cerr << "Readback EnvironmentCubeResource did not enable TransferSrc usage\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateReadbackBuffer() &&
                   validateCopyTextureToBufferContract() &&
                   validateEnvironmentCubeReadbackUsage()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
