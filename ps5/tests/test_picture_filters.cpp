/*
 * PSGopher - the picture's shaders against CPU references: the final blit's Sharp Bilinear, Bicubic
 * (Catmull-Rom) and Lanczos (3 lobes, clamped to the central lobe's texels) filters, enlarging and
 * shrinking; Sharpening (AMD FidelityFX CAS, sharpen-only) and Post Anti-Aliasing (NVIDIA FXAA 3.11,
 * quality preset 29), each drawn on the PC's Vulkan (lavapipe) from the very SPIR-V the title carries
 * (vendor/gopher64/parallel-rdp/psgopher_*_spirv.hpp, made by ps5/tools/make-shaders.sh) on small
 * synthetic pictures, read back as 32-bit floats and compared with the same math in double precision.
 *
 *   test_picture_filters [pictures-folder]   (PPM pictures of each case there when given)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include <volk.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "spirv.hpp" /* vertex_spirv: the blit's vertex shader (data/shader/main.vert) */
#include "psgopher_post_spirv.hpp"
#include "psgopher_resample_spirv.hpp"
#include "psgopher_sharp_spirv.hpp"

static int failures;
#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);   \
			failures++;                                                              \
		}                                                                            \
	} while (0)
#define VK(x)                                                                        \
	do {                                                                             \
		VkResult r_ = (x);                                                           \
		if (r_ != VK_SUCCESS) {                                                      \
			fprintf(stderr, "%s:%d: %s: VkResult %d\n", __FILE__, __LINE__, #x, r_); \
			exit(2);                                                                 \
		}                                                                            \
	} while (0)

/* a picture: RGBA, 0..1 */
struct Picture {
	int w = 0, h = 0;
	std::vector<double> px; /* w * h * 4 */
	double *at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
	const double *at(int x, int y) const { return &px[(size_t(y) * w + x) * 4]; }
};

/* ---- Vulkan on the PC ---- */
static VkInstance instance;
static VkPhysicalDevice gpu;
static VkDevice device;
static VkQueue queue;
static uint32_t queue_family;
static VkCommandPool pool;

static uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want)
{
	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(gpu, &props);
	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
			return i;
	fprintf(stderr, "no memory type\n");
	exit(2);
}

static bool initVulkan()
{
	if (volkInitialize() != VK_SUCCESS)
		return false;
	VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.pApplicationName = "psgopher-picture-filters";
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
		return false;
	volkLoadInstance(instance);
	uint32_t n = 0;
	vkEnumeratePhysicalDevices(instance, &n, nullptr);
	if (!n)
		return false;
	std::vector<VkPhysicalDevice> gpus(n);
	vkEnumeratePhysicalDevices(instance, &n, gpus.data());
	gpu = gpus[0];
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(gpu, &props);
	if (props.apiVersion < VK_API_VERSION_1_3)
		return false;
	printf("test_picture_filters: on %s\n", props.deviceName);
	uint32_t qn = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
	std::vector<VkQueueFamilyProperties> qs(qn);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qs.data());
	queue_family = ~0u;
	for (uint32_t i = 0; i < qn; i++)
		if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queue_family = i;
			break;
		}
	if (queue_family == ~0u)
		return false;
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qci.queueFamilyIndex = queue_family, qci.queueCount = 1, qci.pQueuePriorities = &priority;
	VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	f13.dynamicRendering = VK_TRUE;
	f13.synchronization2 = VK_TRUE;
	VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.pNext = &f13, dci.queueCreateInfoCount = 1, dci.pQueueCreateInfos = &qci;
	if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS)
		return false;
	volkLoadDevice(device);
	vkGetDeviceQueue(device, queue_family, 0, &queue);
	VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	pci.queueFamilyIndex = queue_family;
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK(vkCreateCommandPool(device, &pci, nullptr, &pool));
	return true;
}

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	void *map = nullptr;
};
static Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	Buffer b;
	VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bci.size = size, bci.usage = usage;
	VK(vkCreateBuffer(device, &bci, nullptr, &b.buffer));
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, b.buffer, &req);
	VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK(vkAllocateMemory(device, &mai, nullptr, &b.memory));
	VK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
	VK(vkMapMemory(device, b.memory, 0, size, 0, &b.map));
	return b;
}
static void freeBuffer(Buffer &b)
{
	vkUnmapMemory(device, b.memory);
	vkDestroyBuffer(device, b.buffer, nullptr);
	vkFreeMemory(device, b.memory, nullptr);
}

struct Image {
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};
static Image makeImage(int w, int h, VkFormat format, VkImageUsageFlags usage)
{
	Image im;
	VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.imageType = VK_IMAGE_TYPE_2D, ici.format = format, ici.extent = { uint32_t(w), uint32_t(h), 1 };
	ici.mipLevels = 1, ici.arrayLayers = 1, ici.samples = VK_SAMPLE_COUNT_1_BIT, ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = usage, ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK(vkCreateImage(device, &ici, nullptr, &im.image));
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, im.image, &req);
	VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK(vkAllocateMemory(device, &mai, nullptr, &im.memory));
	VK(vkBindImageMemory(device, im.image, im.memory, 0));
	VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	vci.image = im.image, vci.viewType = VK_IMAGE_VIEW_TYPE_2D, vci.format = format;
	vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VK(vkCreateImageView(device, &vci, nullptr, &im.view));
	return im;
}
static void freeImage(Image &im)
{
	vkDestroyImageView(device, im.view, nullptr);
	vkDestroyImage(device, im.image, nullptr);
	vkFreeMemory(device, im.memory, nullptr);
}

static void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                    VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
	VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	b.srcStageMask = src_stage, b.srcAccessMask = src_access, b.dstStageMask = dst_stage, b.dstAccessMask = dst_access;
	b.oldLayout = from, b.newLayout = to;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image, b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.imageMemoryBarrierCount = 1, di.pImageMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

static VkShaderModule shaderModule(const uint32_t *code, size_t size)
{
	VkShaderModuleCreateInfo sci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	sci.codeSize = size, sci.pCode = code;
	VkShaderModule m;
	VK(vkCreateShaderModule(device, &sci, nullptr, &m));
	return m;
}

/* One full-screen draw of a fragment shader over the blit's vertex shader: the source picture as an
 * 8-bit UNORM texture (or 32-bit float: source_float) through a linear or nearest clamp-to-edge sampler,
 * into a 32-bit float target of dw x dh cleared to 0, in the viewport given (x, y, w, h), with 32 bytes
 * of push constants. The result is read back. */
struct Draw {
	const uint32_t *spirv;
	size_t size;
	bool linear = false;
	bool source_float = false;
	float viewport[4] = { 0, 0, 0, 0 }; /* (w 0: the whole target) */
	float push[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
};
static Picture run(const Picture &src, int dw, int dh, const Draw &d)
{
	const VkFormat src_format = d.source_float ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
	const size_t src_texel = d.source_float ? 16 : 4;
	Image source = makeImage(src.w, src.h, src_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	Image target = makeImage(dw, dh, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	Buffer up = makeBuffer(VkDeviceSize(src.w) * src.h * src_texel, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	Buffer down = makeBuffer(VkDeviceSize(dw) * dh * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	for (size_t i = 0; i < src.px.size(); i++) {
		if (d.source_float)
			((float *)up.map)[i] = float(src.px[i]);
		else
			((uint8_t *)up.map)[i] = uint8_t(std::lround(std::clamp(src.px[i], 0.0, 1.0) * 255.0));
	}

	VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = sci.minFilter = d.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.maxLod = 0.0f;
	VkSampler sampler;
	VK(vkCreateSampler(device, &sci, nullptr, &sampler));

	VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutCreateInfo dlci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dlci.bindingCount = 1, dlci.pBindings = &binding;
	VkDescriptorSetLayout set_layout;
	VK(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &set_layout));
	VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(d.push) };
	VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	plci.setLayoutCount = 1, plci.pSetLayouts = &set_layout, plci.pushConstantRangeCount = 1, plci.pPushConstantRanges = &range;
	VkPipelineLayout layout;
	VK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
	VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
	VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dpci.maxSets = 1, dpci.poolSizeCount = 1, dpci.pPoolSizes = &ps;
	VkDescriptorPool dpool;
	VK(vkCreateDescriptorPool(device, &dpci, nullptr, &dpool));
	VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	dsai.descriptorPool = dpool, dsai.descriptorSetCount = 1, dsai.pSetLayouts = &set_layout;
	VkDescriptorSet set;
	VK(vkAllocateDescriptorSets(device, &dsai, &set));
	VkDescriptorImageInfo dii{ sampler, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet wds{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	wds.dstSet = set, wds.descriptorCount = 1, wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, wds.pImageInfo = &dii;
	vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);

	VkShaderModule vs = shaderModule(vertex_spirv, sizeof(vertex_spirv)), fs = shaderModule(d.spirv, d.size);
	VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT, stages[0].module = vs, stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, stages[1].module = fs, stages[1].pName = "main";
	VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vps.viewportCount = 1, vps.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL, rs.cullMode = VK_CULL_MODE_NONE, rs.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = 0xf;
	VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cb.attachmentCount = 1, cb.pAttachments = &cba;
	const VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	ds.dynamicStateCount = 2, ds.pDynamicStates = dyn;
	const VkFormat target_format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkPipelineRenderingCreateInfo pri{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	pri.colorAttachmentCount = 1, pri.pColorAttachmentFormats = &target_format;
	VkGraphicsPipelineCreateInfo gpci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	gpci.pNext = &pri, gpci.stageCount = 2, gpci.pStages = stages, gpci.pVertexInputState = &vi, gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vps, gpci.pRasterizationState = &rs, gpci.pMultisampleState = &ms, gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &ds, gpci.layout = layout;
	VkPipeline pipeline;
	VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline));

	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cbai.commandPool = pool, cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, cbai.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VK(vkAllocateCommandBuffers(device, &cbai, &cmd));
	VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK(vkBeginCommandBuffer(cmd, &cbbi));
	barrier(cmd, source.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
	        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	VkBufferImageCopy copy{};
	copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copy.imageExtent = { uint32_t(src.w), uint32_t(src.h), 1 };
	vkCmdCopyBufferToImage(cmd, up.buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	barrier(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
	        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	barrier(cmd, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
	        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
	VkRenderingAttachmentInfo att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	att.imageView = target.view, att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	ri.renderArea = { { 0, 0 }, { uint32_t(dw), uint32_t(dh) } }, ri.layerCount = 1, ri.colorAttachmentCount = 1, ri.pColorAttachments = &att;
	vkCmdBeginRendering(cmd, &ri);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(d.push), d.push);
	VkViewport vp{ 0, 0, float(dw), float(dh), 0, 1 };
	if (d.viewport[2] > 0)
		vp = { d.viewport[0], d.viewport[1], d.viewport[2], d.viewport[3], 0, 1 };
	VkRect2D scissor{ { 0, 0 }, { uint32_t(dw), uint32_t(dh) } };
	vkCmdSetViewport(cmd, 0, 1, &vp);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);
	barrier(cmd, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
	        VK_ACCESS_2_TRANSFER_READ_BIT);
	copy.imageExtent = { uint32_t(dw), uint32_t(dh), 1 };
	vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, down.buffer, 1, &copy);
	VK(vkEndCommandBuffer(cmd));
	VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	si.commandBufferCount = 1, si.pCommandBuffers = &cmd;
	VK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
	VK(vkQueueWaitIdle(queue));

	Picture out;
	out.w = dw, out.h = dh, out.px.resize(size_t(dw) * dh * 4);
	for (size_t i = 0; i < out.px.size(); i++)
		out.px[i] = ((const float *)down.map)[i];

	vkFreeCommandBuffers(device, pool, 1, &cmd);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyShaderModule(device, vs, nullptr);
	vkDestroyShaderModule(device, fs, nullptr);
	vkDestroyDescriptorPool(device, dpool, nullptr);
	vkDestroyPipelineLayout(device, layout, nullptr);
	vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
	vkDestroySampler(device, sampler, nullptr);
	freeBuffer(up);
	freeBuffer(down);
	freeImage(source);
	freeImage(target);
	return out;
}

/* ---- the pictures ---- */
static Picture quantised(Picture p) /* (as the 8-bit texture holds it) */
{
	for (double &v : p.px)
		v = std::lround(std::clamp(v, 0.0, 1.0) * 255.0) / 255.0;
	return p;
}
/* hard edges, a gradient, single bright and dark texels, a checker: every kind of detail */
static Picture testPicture(int w, int h, unsigned seed)
{
	Picture p;
	p.w = w, p.h = h, p.px.resize(size_t(w) * h * 4);
	unsigned s = seed;
	auto rnd = [&s] { s = s * 1103515245u + 12345u; return double((s >> 8) & 0xffff) / 65535.0; };
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			double *c = p.at(x, y);
			if (x < w / 3) /* a hard vertical edge and a horizontal one */
				c[0] = x < w / 6 ? 0.9 : 0.1, c[1] = y < h / 2 ? 0.8 : 0.2, c[2] = 0.5;
			else if (x < 2 * w / 3) /* a gradient with a checker over it */
				c[0] = double(x) / w, c[1] = ((x + y) & 1) ? 0.95 : 0.05, c[2] = double(y) / h;
			else /* noise */
				c[0] = rnd(), c[1] = rnd(), c[2] = rnd();
			c[3] = 1.0;
		}
	*p.at(w / 2, h / 4) = 1.0; /* (a lone bright texel's red) */
	return quantised(p);
}
static Picture flatPicture(int w, int h, double r, double g, double b)
{
	Picture p;
	p.w = w, p.h = h, p.px.resize(size_t(w) * h * 4);
	for (int i = 0; i < w * h; i++)
		p.px[size_t(i) * 4 + 0] = r, p.px[size_t(i) * 4 + 1] = g, p.px[size_t(i) * 4 + 2] = b, p.px[size_t(i) * 4 + 3] = 1.0;
	return quantised(p);
}

/* ---- CPU references ---- */
static const double kPi = 3.14159265358979323846;
static double catmullRom(double x)
{
	x = std::fabs(x);
	if (x < 1.0)
		return (1.5 * x - 2.5) * x * x + 1.0;
	if (x < 2.0)
		return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
	return 0.0;
}
static double lanczos3(double x)
{
	x = std::fabs(x);
	if (x >= 3.0)
		return 0.0;
	if (x < 1e-5)
		return 1.0;
	const double px = kPi * x;
	return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}
/* the resample shaders' math: vUV = the output pixel's centre over the target (the viewport is the
 * whole target), the kernel widened by the source texels per output pixel (at least 1, at most
 * 24 / (2 * radius)), normalised weights, the texel coordinates clamped; Lanczos clamped to the
 * texels with |i - t| < 0.999 s on both axes */
static Picture resampleReference(const Picture &src, int dw, int dh, bool lanczos)
{
	const double radius = lanczos ? 3.0 : 2.0;
	Picture out;
	out.w = dw, out.h = dh, out.px.resize(size_t(dw) * dh * 4);
	const double sx = std::clamp(double(src.w) / dw, 1.0, 24.0 / (2.0 * radius));
	const double sy = std::clamp(double(src.h) / dh, 1.0, 24.0 / (2.0 * radius));
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++) {
			const double tx = (x + 0.5) / dw * src.w - 0.5, ty = (y + 0.5) / dh * src.h - 0.5;
			double sum[4] = { 0, 0, 0, 0 }, lo[4] = { 1e9, 1e9, 1e9, 1e9 }, hi[4] = { -1e9, -1e9, -1e9, -1e9 };
			double wsum = 0;
			for (int j = int(std::floor(ty - radius * sy)) + 1; j < std::ceil(ty + radius * sy); j++)
				for (int i = int(std::floor(tx - radius * sx)) + 1; i < std::ceil(tx + radius * sx); i++) {
					const double wgt = lanczos ? lanczos3((i - tx) / sx) * lanczos3((j - ty) / sy)
					                           : catmullRom((i - tx) / sx) * catmullRom((j - ty) / sy);
					const double *c = src.at(std::clamp(i, 0, src.w - 1), std::clamp(j, 0, src.h - 1));
					const bool central = std::fabs(i - tx) < 0.999 * sx && std::fabs(j - ty) < 0.999 * sy;
					for (int k = 0; k < 4; k++) {
						sum[k] += wgt * c[k];
						if (central)
							lo[k] = std::min(lo[k], c[k]), hi[k] = std::max(hi[k], c[k]);
					}
					wsum += wgt;
				}
			double *o = out.at(x, y);
			for (int k = 0; k < 4; k++) {
				double v = sum[k] / wsum;
				if (lanczos)
					v = std::clamp(v, lo[k], hi[k]);
				o[k] = std::clamp(v, 0.0, 1.0);
			}
		}
	return out;
}
/* Sharp Bilinear (psgopher_sharp.frag) with the linear sampler's bilinear blend */
static void bilinear(const Picture &src, double u, double v, double *out)
{
	const double x = u * src.w - 0.5, y = v * src.h - 0.5;
	const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
	const double fx = x - x0, fy = y - y0;
	for (int k = 0; k < 4; k++) {
		auto t = [&](int i, int j) { return src.at(std::clamp(i, 0, src.w - 1), std::clamp(j, 0, src.h - 1))[k]; };
		out[k] = (t(x0, y0) * (1 - fx) + t(x0 + 1, y0) * fx) * (1 - fy) + (t(x0, y0 + 1) * (1 - fx) + t(x0 + 1, y0 + 1) * fx) * fy;
	}
}
static Picture sharpReference(const Picture &src, int dw, int dh)
{
	Picture out;
	out.w = dw, out.h = dh, out.px.resize(size_t(dw) * dh * 4);
	const double scale[2] = { std::max(double(dw) / src.w, 1.0), std::max(double(dh) / src.h, 1.0) };
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++) {
			const double uv[2] = { (x + 0.5) / dw, (y + 0.5) / dh }, size[2] = { double(src.w), double(src.h) };
			double at[2];
			for (int a = 0; a < 2; a++) {
				const double texel = uv[a] * size[a], range = 0.5 - 0.5 / scale[a], offset = texel - std::floor(texel) - 0.5;
				const double inside = (offset - std::clamp(offset, -range, range)) * scale[a] + 0.5;
				at[a] = (std::floor(texel) + inside) / size[a];
			}
			bilinear(src, at[0], at[1], out.at(x, y));
		}
	return out;
}
/* CAS (psgopher_cas.frag: AMD's CasFilter, noScaling, green weight, exact reciprocals) */
static Picture casReference(const Picture &src, const int rect[4], double sharpness)
{
	const double peak = -1.0 / (8.0 + (5.0 - 8.0) * sharpness);
	Picture out;
	out.w = src.w, out.h = src.h, out.px.assign(size_t(src.w) * src.h * 4, 0.0);
	for (int y = rect[1]; y < rect[3]; y++)
		for (int x = rect[0]; x < rect[2]; x++) {
			auto load = [&](int dx, int dy, int k) {
				const double c = src.at(std::clamp(x + dx, rect[0], rect[2] - 1), std::clamp(y + dy, rect[1], rect[3] - 1))[k];
				return c * c;
			};
			double b[3], d[3], e[3], f[3], h[3], amp[3];
			for (int k = 0; k < 3; k++) {
				b[k] = load(0, -1, k), d[k] = load(-1, 0, k), e[k] = load(0, 0, k), f[k] = load(1, 0, k), h[k] = load(0, 1, k);
				const double mn = std::min({ d[k], e[k], f[k], b[k], h[k] }), mx = std::max({ d[k], e[k], f[k], b[k], h[k] });
				amp[k] = std::sqrt(std::clamp(std::min(mn, 1.0 - mx) / std::max(mx, 1e-20), 0.0, 1.0));
			}
			const double w = amp[1] * peak, rcp = 1.0 / (1.0 + 4.0 * w);
			double *o = out.at(x, y);
			for (int k = 0; k < 3; k++)
				o[k] = std::sqrt(std::clamp(((b[k] + d[k] + f[k] + h[k]) * w + e[k]) * rcp, 0.0, 1.0));
			o[3] = 1.0;
		}
	return out;
}
/* FXAA 3.11 Quality, preset 29 (psgopher_fxaa.frag), with a bilinear sampler and the positions clamped
 * to the rectangle; early[] says which pixels took the early exit (no edge: FXAA's edge detection), tie[]
 * which ones had the edge's direction or the early exit decided by a tie (within 1e-6: a checker's
 * horizontal and vertical edges are exactly as strong), where 32-bit floats may go the other way */
static Picture fxaaReference(const Picture &src, const int rect[4], std::vector<char> *early, std::vector<char> *tie)
{
	const double rcp[2] = { 1.0 / src.w, 1.0 / src.h };
	const double lo[2] = { (rect[0] + 0.5) * rcp[0], (rect[1] + 0.5) * rcp[1] }, hi[2] = { (rect[2] - 0.5) * rcp[0], (rect[3] - 0.5) * rcp[1] };
	auto top = [&](double u, double v, double *c) { bilinear(src, std::clamp(u, lo[0], hi[0]), std::clamp(v, lo[1], hi[1]), c); };
	auto luma = [&](double u, double v) {
		double c[4];
		top(u, v, c);
		return c[0] * 0.299 + c[1] * 0.587 + c[2] * 0.114;
	};
	static const double P[12] = { 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };
	Picture out;
	out.w = src.w, out.h = src.h, out.px.assign(size_t(src.w) * src.h * 4, 0.0);
	early->assign(size_t(src.w) * src.h, 0);
	tie->assign(size_t(src.w) * src.h, 0);
	for (int y = rect[1]; y < rect[3]; y++)
		for (int x = rect[0]; x < rect[2]; x++) {
			double posM[2] = { (x + 0.5) * rcp[0], (y + 0.5) * rcp[1] };
			double *o = out.at(x, y);
			double rgbyM[4];
			top(posM[0], posM[1], rgbyM);
			const double lumaM = rgbyM[0] * 0.299 + rgbyM[1] * 0.587 + rgbyM[2] * 0.114;
			double lumaS = luma(posM[0], posM[1] + rcp[1]), lumaE = luma(posM[0] + rcp[0], posM[1]);
			double lumaN = luma(posM[0], posM[1] - rcp[1]), lumaW = luma(posM[0] - rcp[0], posM[1]);
			const double rangeMax = std::max({ lumaS, lumaM, lumaE, lumaN, lumaW }), rangeMin = std::min({ lumaS, lumaM, lumaE, lumaN, lumaW });
			const double range = rangeMax - rangeMin;
			char &tied = (*tie)[size_t(y) * src.w + x];
			tied = std::fabs(range - std::max(0.0833, rangeMax * 0.166)) < 1e-6;
			if (range < std::max(0.0833, rangeMax * 0.166)) {
				(*early)[size_t(y) * src.w + x] = 1;
				o[0] = rgbyM[0], o[1] = rgbyM[1], o[2] = rgbyM[2], o[3] = 1.0;
				continue;
			}
			const double lumaNW = luma(posM[0] - rcp[0], posM[1] - rcp[1]), lumaSE = luma(posM[0] + rcp[0], posM[1] + rcp[1]);
			const double lumaNE = luma(posM[0] + rcp[0], posM[1] - rcp[1]), lumaSW = luma(posM[0] - rcp[0], posM[1] + rcp[1]);
			const double edgeHorz = std::fabs(-2.0 * lumaW + lumaNW + lumaSW) + std::fabs(-2.0 * lumaM + lumaN + lumaS) * 2.0 +
			                        std::fabs(-2.0 * lumaE + lumaNE + lumaSE);
			const double edgeVert = std::fabs(-2.0 * lumaS + lumaSW + lumaSE) + std::fabs(-2.0 * lumaM + lumaW + lumaE) * 2.0 +
			                        std::fabs(-2.0 * lumaN + lumaNW + lumaNE);
			const bool horzSpan = edgeHorz >= edgeVert;
			tied |= std::fabs(edgeHorz - edgeVert) < 1e-6;
			const double subpixA = (lumaN + lumaS + lumaW + lumaE) * 2.0 + (lumaNW + lumaSW + lumaNE + lumaSE);
			double lengthSign = horzSpan ? rcp[1] : rcp[0];
			if (!horzSpan)
				lumaN = lumaW, lumaS = lumaE;
			const double subpixB = subpixA / 12.0 - lumaM;
			const double gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;
			const bool pairN = std::fabs(gradientN) >= std::fabs(gradientS);
			const double gradient = std::max(std::fabs(gradientN), std::fabs(gradientS));
			if (pairN)
				lengthSign = -lengthSign;
			const double subpixC = std::clamp(std::fabs(subpixB) / range, 0.0, 1.0);
			double posB[2] = { posM[0], posM[1] };
			const double off[2] = { horzSpan ? rcp[0] : 0.0, horzSpan ? 0.0 : rcp[1] };
			posB[horzSpan ? 1 : 0] += lengthSign * 0.5;
			double posN[2] = { posB[0] - off[0] * P[0], posB[1] - off[1] * P[0] }, posP[2] = { posB[0] + off[0] * P[0], posB[1] + off[1] * P[0] };
			const double lumaNN = (pairN ? lumaN : lumaS) + lumaM;
			const double gradientScaled = gradient / 4.0;
			const bool lumaMLTZero = lumaM - lumaNN * 0.5 < 0.0;
			double endN = luma(posN[0], posN[1]) - lumaNN * 0.5, endP = luma(posP[0], posP[1]) - lumaNN * 0.5;
			bool doneN = std::fabs(endN) >= gradientScaled, doneP = std::fabs(endP) >= gradientScaled;
			for (int k = 1; k < 12; k++) {
				if (!doneN)
					posN[0] -= off[0] * P[k], posN[1] -= off[1] * P[k];
				if (!doneP)
					posP[0] += off[0] * P[k], posP[1] += off[1] * P[k];
				if ((doneN && doneP) || k == 11)
					break;
				if (!doneN)
					endN = luma(posN[0], posN[1]) - lumaNN * 0.5;
				if (!doneP)
					endP = luma(posP[0], posP[1]) - lumaNN * 0.5;
				doneN = std::fabs(endN) >= gradientScaled, doneP = std::fabs(endP) >= gradientScaled;
			}
			const int a = horzSpan ? 0 : 1;
			const double dstN = posM[a] - posN[a], dstP = posP[a] - posM[a];
			const bool goodSpan = dstN < dstP ? ((endN < 0.0) != lumaMLTZero) : ((endP < 0.0) != lumaMLTZero);
			const double pixelOffset = -std::min(dstN, dstP) / (dstN + dstP) + 0.5;
			const double subpixF = (-2.0 * subpixC + 3.0) * subpixC * subpixC;
			const double offset = std::max(goodSpan ? pixelOffset : 0.0, subpixF * subpixF * 0.75);
			posM[horzSpan ? 1 : 0] += offset * lengthSign;
			double c[4];
			top(posM[0], posM[1], c);
			o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = 1.0;
		}
	return out;
}

/* ---- the comparisons ---- */
struct Diff {
	double max = 0;
	int over = 0, skipped = 0;
};
static Diff compare(const Picture &a, const Picture &b, double tolerance, int channels = 4, const std::vector<char> *skip = nullptr)
{
	Diff d;
	for (int y = 0; y < a.h; y++)
		for (int x = 0; x < a.w; x++)
			for (int k = 0; k < channels; k++) {
				const double e = std::fabs(a.at(x, y)[k] - b.at(x, y)[k]);
				if (skip && (*skip)[size_t(y) * a.w + x]) { /* (may differ: counted apart) */
					d.skipped += e > tolerance;
					continue;
				}
				d.max = std::max(d.max, e);
				d.over += e > tolerance;
			}
	return d;
}
static std::string pictures;
static void save(const Picture &p, const char *name)
{
	if (pictures.empty())
		return;
	FILE *f = fopen((pictures + "/" + name + ".ppm").c_str(), "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", p.w, p.h);
	for (int i = 0; i < p.w * p.h; i++)
		for (int k = 0; k < 3; k++)
			fputc(int(std::lround(std::clamp(p.px[size_t(i) * 4 + k], 0.0, 1.0) * 255.0)), f);
	fclose(f);
}
static void report(const char *what, const Diff &d, double tolerance)
{
	printf("  %-46s max difference %.2e (tolerance %.0e)%s\n", what, d.max, tolerance, d.over ? "  FAILED" : "");
	CHECK(d.over == 0);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		pictures = argv[1];
	if (!initVulkan()) {
		printf("test_picture_filters: skipped (no Vulkan 1.3 device: lavapipe, VK_ICD_FILENAMES)\n");
		return 0;
	}
	const double tol = 2e-5; /* (32-bit floats against doubles; texelFetch reads exactly) */

	/* the final filters: enlarged by 2.3x (an uneven scale), at 1:1, shrunk by about 2.3x and by 7x
	   (past the Lanczos widening's cap of 4 and Catmull-Rom's of 6) */
	const Picture small = testPicture(16, 12, 1), large = testPicture(64, 48, 2);
	struct Case {
		const char *name;
		const Picture *src;
		int dw, dh;
	} cases[] = { { "enlarged 16x12 -> 37x28", &small, 37, 28 }, { "1:1 16x12", &small, 16, 12 },
		          { "shrunk 16x12 -> 7x5", &small, 7, 5 }, { "shrunk 64x48 -> 9x7", &large, 9, 7 },
		          { "shrunk 64x48 -> 27x20", &large, 27, 20 } };
	printf("test_picture_filters: Bicubic (Catmull-Rom) and Lanczos 3 against the CPU reference\n");
	for (const Case &c : cases)
		for (int lanczos = 0; lanczos < 2; lanczos++) {
			Draw d;
			d.spirv = lanczos ? psgopher_lanczos_fragment_spirv : psgopher_bicubic_fragment_spirv;
			d.size = lanczos ? sizeof(psgopher_lanczos_fragment_spirv) : sizeof(psgopher_bicubic_fragment_spirv);
			d.linear = true; /* (the title's sampler; texelFetch does not use it) */
			const Picture gpu_out = run(*c.src, c.dw, c.dh, d), ref = resampleReference(*c.src, c.dw, c.dh, lanczos);
			const std::string what = std::string(lanczos ? "Lanczos " : "Bicubic ") + c.name;
			report(what.c_str(), compare(gpu_out, ref, tol), tol);
			std::string file = std::string(lanczos ? "lanczos-" : "bicubic-") + std::to_string(c.dw) + "x" + std::to_string(c.dh);
			save(gpu_out, file.c_str());
			if (c.dw == c.src->w) /* at 1:1 both are exact: the kernels are 1 at 0 and 0 at the other texels */
				report((what + " equals the source").c_str(), compare(gpu_out, *c.src, tol), tol);
		}
	/* Lanczos never leaves the 2x2 neighbourhood when enlarging (no ringing); Catmull-Rom does at a hard edge */
	{
		Draw d;
		d.spirv = psgopher_lanczos_fragment_spirv, d.size = sizeof(psgopher_lanczos_fragment_spirv);
		const Picture out = run(small, 37, 28, d);
		int outside = 0;
		for (int y = 0; y < 28; y++)
			for (int x = 0; x < 37; x++) {
				const double tx = (x + 0.5) / 37 * 16 - 0.5, ty = (y + 0.5) / 28 * 12 - 0.5;
				const int x0 = std::clamp(int(std::floor(tx)), 0, 15), y0 = std::clamp(int(std::floor(ty)), 0, 11);
				const int x1 = std::min(x0 + 1, 15), y1 = std::min(y0 + 1, 11);
				for (int k = 0; k < 3; k++) {
					const double v[4] = { small.at(x0, y0)[k], small.at(x1, y0)[k], small.at(x0, y1)[k], small.at(x1, y1)[k] };
					const double o = out.at(x, y)[k];
					outside += o < *std::min_element(v, v + 4) - tol || o > *std::max_element(v, v + 4) + tol;
				}
			}
		printf("  Lanczos enlarged: %d values outside their 2x2 neighbourhood\n", outside);
		CHECK(outside == 0);
	}
	/* a flat picture stays flat under every filter, enlarged or shrunk */
	{
		const Picture flat = flatPicture(13, 9, 0.25, 0.5, 0.75);
		for (int which = 0; which < 3; which++) {
			Draw d;
			d.spirv = which == 0 ? psgopher_bicubic_fragment_spirv : which == 1 ? psgopher_lanczos_fragment_spirv : psgopher_sharp_fragment_spirv;
			d.size = which == 0 ? sizeof(psgopher_bicubic_fragment_spirv) : which == 1 ? sizeof(psgopher_lanczos_fragment_spirv) : sizeof(psgopher_sharp_fragment_spirv);
			d.linear = true;
			for (int dims = 0; dims < 2; dims++) {
				const int dw = dims ? 5 : 31, dh = dims ? 4 : 22;
				const Picture out = run(flat, dw, dh, d);
				Picture expect = flatPicture(dw, dh, 0.25, 0.5, 0.75);
				char what[96];
				snprintf(what, sizeof(what), "%s flat %s stays flat", which == 0 ? "Bicubic" : which == 1 ? "Lanczos" : "Sharp Bilinear",
				         dims ? "shrunk" : "enlarged");
				report(what, compare(out, expect, 1e-5), 1e-5);
			}
		}
	}
	/* Sharp Bilinear (the default) against its reference: through the linear sampler, whose
	   sub-texel weights are 8-bit on lavapipe, hence the wider tolerance */
	{
		Draw d;
		d.spirv = psgopher_sharp_fragment_spirv, d.size = sizeof(psgopher_sharp_fragment_spirv), d.linear = true;
		for (const Case &c : cases) {
			const Picture out = run(*c.src, c.dw, c.dh, d), ref = sharpReference(*c.src, c.dw, c.dh);
			report((std::string("Sharp Bilinear ") + c.name).c_str(), compare(out, ref, 8e-3), 8e-3);
		}
	}

	/* Sharpening: CAS at AMD's sharpness 0, 0.5 and 1 (Low, Medium, High), on the whole picture and on
	   a rectangle inside it (the letterbox: outside it nothing is drawn, and it is not read) */
	printf("test_picture_filters: Sharpening (FidelityFX CAS) against the CPU reference\n");
	{
		const Picture src = testPicture(24, 18, 3);
		const double sharpness[] = { 0.0, 0.5, 1.0 };
		const int rects[2][4] = { { 0, 0, 24, 18 }, { 3, 2, 21, 15 } };
		for (const auto &rect : rects)
			for (double s : sharpness) {
				Draw d;
				d.spirv = psgopher_cas_fragment_spirv, d.size = sizeof(psgopher_cas_fragment_spirv);
				d.viewport[0] = float(rect[0]), d.viewport[1] = float(rect[1]);
				d.viewport[2] = float(rect[2] - rect[0]), d.viewport[3] = float(rect[3] - rect[1]);
				d.push[0] = float(rect[0]), d.push[1] = float(rect[1]), d.push[2] = float(rect[2]), d.push[3] = float(rect[3]);
				d.push[4] = float(-1.0 / (8.0 + (5.0 - 8.0) * s));
				/* the letterbox's black around the rectangle in the source, as the scaled picture has it */
				Picture boxed = src;
				for (int y = 0; y < src.h; y++)
					for (int x = 0; x < src.w; x++)
						if (x < rect[0] || x >= rect[2] || y < rect[1] || y >= rect[3])
							boxed.at(x, y)[0] = boxed.at(x, y)[1] = boxed.at(x, y)[2] = 0.0;
				const Picture out = run(boxed, src.w, src.h, d), ref = casReference(boxed, rect, s);
				char what[96];
				snprintf(what, sizeof(what), "CAS sharpness %.1f, rectangle %d,%d-%d,%d", s, rect[0], rect[1], rect[2], rect[3]);
				report(what, compare(out, ref, 1e-4), 1e-4);
				if (rect[0] == 0 && s == 1.0)
					save(out, "cas-high");
			}
		/* a flat picture stays flat */
		const Picture flat = flatPicture(10, 8, 0.2, 0.6, 0.9);
		Draw d;
		d.spirv = psgopher_cas_fragment_spirv, d.size = sizeof(psgopher_cas_fragment_spirv);
		d.push[2] = 10, d.push[3] = 8, d.push[4] = -0.2f;
		report("CAS flat stays flat", compare(run(flat, 10, 8, d), flat, 1e-4, 3), 1e-4);
		save(src, "source");
	}

	/* Post Anti-Aliasing: FXAA 3.11 preset 29 on a staircase edge and the test picture (32-bit float
	   texels, so the bilinear sampler's sub-texel weights are not 8-bit), against the reference: which
	   pixels it leaves alone (its edge detection) and what it gives the others */
	printf("test_picture_filters: Post Anti-Aliasing (FXAA 3.11, preset 29) against the CPU reference\n");
	{
		Picture stairs;
		stairs.w = 40, stairs.h = 30, stairs.px.resize(40 * 30 * 4);
		for (int y = 0; y < 30; y++)
			for (int x = 0; x < 40; x++) {
				double *c = stairs.at(x, y);
				const bool in = x * 3 < y * 4 + 5; /* an aliased edge, 4:3 steep */
				c[0] = in ? 0.9 : 0.1, c[1] = in ? 0.85 : 0.15, c[2] = in ? 0.8 : 0.2, c[3] = 1.0;
			}
		stairs = quantised(stairs);
		const Picture pic = testPicture(32, 24, 4);
		const int full_stairs[4] = { 0, 0, 40, 30 }, inner_stairs[4] = { 4, 3, 36, 27 }, full_pic[4] = { 0, 0, 32, 24 };
		struct F {
			const char *name;
			const Picture *src;
			const int *rect;
		} fs[] = { { "staircase", &stairs, full_stairs }, { "staircase in a rectangle", &stairs, inner_stairs }, { "test picture", &pic, full_pic } };
		for (const F &f : fs) {
			Draw d;
			d.spirv = psgopher_fxaa_fragment_spirv, d.size = sizeof(psgopher_fxaa_fragment_spirv);
			d.linear = true, d.source_float = true;
			d.viewport[0] = float(f.rect[0]), d.viewport[1] = float(f.rect[1]);
			d.viewport[2] = float(f.rect[2] - f.rect[0]), d.viewport[3] = float(f.rect[3] - f.rect[1]);
			for (int k = 0; k < 4; k++)
				d.push[k] = float(f.rect[k]);
			const Picture out = run(*f.src, f.src->w, f.src->h, d);
			std::vector<char> early, tie;
			const Picture ref = fxaaReference(*f.src, f.rect, &early, &tie);
			int edges = 0, changed = 0, early_changed = 0;
			for (int y = f.rect[1]; y < f.rect[3]; y++)
				for (int x = f.rect[0]; x < f.rect[2]; x++) {
					const bool moved = std::fabs(out.at(x, y)[0] - f.src->at(x, y)[0]) > 1e-6 || std::fabs(out.at(x, y)[1] - f.src->at(x, y)[1]) > 1e-6;
					edges += !early[size_t(y) * f.src->w + x];
					changed += moved;
					early_changed += moved && early[size_t(y) * f.src->w + x];
				}
			const Diff diff = compare(out, ref, 1e-4, 3, &tie);
			printf("  FXAA %s: %d edge pixels found, %d changed, %d changed outside the edges; %d values differ where a tie decided\n", f.name,
			       edges, changed, early_changed, diff.skipped);
			CHECK(edges > 0 && changed > 0 && early_changed == 0);
			report((std::string("FXAA ") + f.name).c_str(), diff, 1e-4);
			if (f.rect == full_stairs) {
				save(*f.src, "fxaa-source");
				save(out, "fxaa");
			}
		}
	}

	vkDestroyCommandPool(device, pool, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
	printf("test_picture_filters: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
